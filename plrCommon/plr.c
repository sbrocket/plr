#include "plr.h"
#include "plrSharedData.h"
#include "timeUtil.h"
#include "pthreadUtil.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <assert.h>
#include <signal.h>
#include <unistd.h>
#include <sys/prctl.h>

///////////////////////////////////////////////////////////////////////////////
// Global data

// Used to avoid recursion when PLR code calls libc syscalls
int insidePLRCode = 0;

///////////////////////////////////////////////////////////////////////////////
// Private functions

// Create a new redundant PLR process as a copy of the calling process.
// plrShm->lock must be held while calling this, and is held by each process
// when it returns from this.
int plr_forkNewProcess(perProcData_t *newProcShm);

// 
typedef int (*barrierActFptr)(void);
int plr_waitBarrier(barrierActFptr actionFptr);

// Handle expired watchdog timer during plr_waitBarrier
int plr_watchdogExpired();

///////////////////////////////////////////////////////////////////////////////

int plr_figureheadInit(int nProc) {
  // Set figurehead process as subreaper so grandchild processes
  // get reparented to it, instead of init
  if (prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) < 0) {
    fprintf(stderr, "Error: prctl PR_SET_CHILD_SUBREAPER failed\n");
    return -1;
  }
  
  if (plrSD_initSharedData(nProc) < 0) {
    fprintf(stderr, "Error: PLR Shared data init failed\n");
    return -1;
  }
  return 0;
}

///////////////////////////////////////////////////////////////////////////////

int plr_figureheadExit() {
  if (plrSD_cleanupSharedData() < 0) {
    fprintf(stderr, "Error: PLR shared data cleanup failed\n");
    return -1;
  }
  return 0;
}

///////////////////////////////////////////////////////////////////////////////

int plr_processInit() {
  insidePLRCode = 1;
  
  if (plrSD_acquireSharedData() < 0) {
    fprintf(stderr, "Error: PLR shared data acquire failed\n");
    return -1;
  }
  //printf("[%d] plr: &plrShm = %p, plrShm = %p\n", getpid(), &plrShm, plrShm);
  
  if (myProcShm) {
    printf("[%d] Called init but already has myProcShm\n", getpid());
    return 0;
  }
  
  // Lock shared data mutex while modifying data
  pthread_mutex_lock(&plrShm->lock);
  
  // Initialize per-proc data for this first process
  if (myProcShm == NULL && allProcShm[0].pid == 0) {
    myProcShm = &allProcShm[0];
    plrSD_initProcData(myProcShm);
  }
  
  // Fork missing redundant processes and init their per-proc data areas
  int myPid = getpid();
  for (int i = 0; i < plrShm->nProc; ++i) {
    int iPid = allProcShm[i].pid;
    if (iPid == 0) {
      if (plr_forkNewProcess(&allProcShm[i]) < 0) {
        fprintf(stderr, "[%d] plr_forkNewProcess failed\n", myPid);
      }
      if (myProcShm == &allProcShm[i]) {
        // This is the child that was just created, break out of loop
        myPid = allProcShm[i].pid;
        break;
      }
    } else if (iPid == myPid) {
      // Set myProcShm to the right struct in case it's not set
      myProcShm = &allProcShm[i];
    }
  }
  
  pthread_mutex_unlock(&plrShm->lock);
  insidePLRCode = 0;
  return 0;
}

///////////////////////////////////////////////////////////////////////////////

int plr_checkSyscall() {
  if (insidePLRCode) {
    return 0;
  }
  insidePLRCode = 1;
  
  plr_waitBarrier(NULL);
  
  insidePLRCode = 0;
  return 0;
}

///////////////////////////////////////////////////////////////////////////////

int plr_waitBarrier(barrierActFptr actionFptr) {  
  pthread_mutex_lock(&plrShm->lock);
  
  // Ignore calls to plr_wait that come from the wrong pid, seems to
  // occur when also instrumenting binary with Pin
  if (myProcShm->pid != getpid()) {
    pthread_mutex_unlock(&plrShm->lock);
    return 0;
  }
  
  // Mark this process as waiting at barrier
  int waitIdx = plrShm->curWaitIdx;
  assert(plrShm->condWaitCnt[waitIdx] <= plrShm->nProc);
  myProcShm->waitIdx = waitIdx;
  plrShm->condWaitCnt[waitIdx]++;
  
  // If this isn't the first process to wait, wake up all other waiting
  // processes so their timers restart (to avoid early watchdog timeout)
  if (plrShm->condWaitCnt[waitIdx] > 1 && plrShm->condWaitCnt[waitIdx] < plrShm->nProc) {
    for (int i = 0; i < plrShm->nProc; ++i) {
      // pthread_cond_signal does nothing if no threads waiting on it,
      // so safe to call on all procs, waiting or not
      pthread_cond_signal(&allProcShm[i].cond[waitIdx]);
    }
  }
  
  // Wait until all processes have reached this barrier
  int watchdogExpired = 0;
  while (1) {
    // Check barrier exit conditions
    if (myProcShm->waitIdx < 0) {
      // Wait flag already cleared, break out of wait loop
      break;
    }
    if (myProcShm->waitIdx >= 0 && plrShm->condWaitCnt[waitIdx] == plrShm->nProc) {
      // Exit condition is met but wait flag has not been removed
      // Call barrier action through provided function ptr now that all
      // processes are synchronized & waiting at the barrier
      int actionSuccess = 0;
      if (actionFptr) {
        int ret = actionFptr();
        actionSuccess = (ret >= 0);
      } else {
        // No action function ptr provided, default success once exit condition met
        actionSuccess = 1;
      }
      
      // If barrier action succeeds, wake up all other processes & leave barrier
      if (actionSuccess) {
        // Shift curWaitIdx to other value so subsequent plr_wait calls use the
        // other condition variable
        plrShm->curWaitIdx = (plrShm->curWaitIdx) ? 0 : 1;
        
        // Reset all wait flags and wake up all other processes
        //printf("[%d] Signaling all processes to wake up for idx %d\n", getpid(), waitIdx);
        for (int i = 0; i < plrShm->nProc; ++i) {
          pthread_cond_signal(&allProcShm[i].cond[waitIdx]);
          allProcShm[i].waitIdx = -1;
        }
        
        // Break out of wait loop
        break;
      }
    }
    
    // Check if watchdog expired
    if (watchdogExpired) {
      int ret = plr_watchdogExpired();
      if (ret < 0) {
        myProcShm->waitIdx = -1;
        plrShm->condWaitCnt[waitIdx]--;
        pthread_mutex_unlock(&plrShm->lock);
        exit(1);
      } else if (ret == 1) {
        // Forked new process to replace stuck one, check barrier exit condition again
        continue;
      }
    }
    
    // Must use CLOCK_REALTIME, _timedwait needs abstime since epoch
    struct timespec absWait;
    clock_gettime(CLOCK_REALTIME, &absWait);
    absWait = tspecAddMs(absWait, 200);
        
    // Using _timedwait as a watchdog timer, to avoid deadlock in case one of the
    // redundant processes has died or is stuck
    // NOTE: This ignores the case of pthread_cond_timedwait returning EINTR, in
    // which case less time than specified has elapsed. This can result in a longer
    // than desired wait time because timer may just restart.

    int ret = pthread_cond_timedwait(&myProcShm->cond[waitIdx], &plrShm->lock, &absWait);
    if (ret == ETIMEDOUT) {
      // Loop again to make sure timer didn't expire while last proc was waiting
      watchdogExpired = 1; 
    } else if (ret != 0) {
      fprintf(stderr, "[%d] pthread_cond_timedwait returned %d\n", getpid(), ret);
    }
  }
  
  // Decrement waiting process counter
  plrShm->condWaitCnt[waitIdx]--;
  
  pthread_mutex_unlock(&plrShm->lock);
  return 0;
}

///////////////////////////////////////////////////////////////////////////////

// Return value:
//   < 0 : Error occured
//     0 : No replacement process created
//     1 : Replacement process created
int plr_watchdogExpired() {
  // If another process already started restoration, don't do anything
  if (plrShm->restoring) {
    return 0;
  }
  
  // Check if more than one process failed to wait
  int waitIdx = plrShm->curWaitIdx;
  if (plrShm->nProc - plrShm->condWaitCnt[waitIdx] > 1) {
    // More than 1 process is not waiting, can't recover
    fprintf(stderr, "[%d] Error: Watchdog expired & more than 1 process is not waiting - unrecoverable\n", myProcShm->pid);
    return -1;
  }
  
  // Replace faulted process with a copy of the current process
  int didReplace = 0;
  for (int i = 0; i < plrShm->nProc; ++i) {
    if (allProcShm[i].waitIdx < 0) {
      printf("[%d] Pid %d failed to wait before watchdog expired\n", myProcShm->pid, allProcShm[i].pid);
      plrShm->restoring = 1;
      didReplace = 1;
      
      // Kill stuck process (if not already dead)
      kill(allProcShm[i].pid, SIGKILL);
      if (plrSD_freeProcData(&allProcShm[i]) < 0) {
        fprintf(stderr, "[%d] plrSD_freeProcData failed\n", myProcShm->pid);
        return -1;
      }
      
      // Fork replacement process from current good process
      if (plr_forkNewProcess(&allProcShm[i]) < 0) {
        fprintf(stderr, "[%d] plr_forkNewProcess failed\n", myProcShm->pid);
        return -1;
      }

      // If myProcShm equals the area for the process that was just killed,
      // then this is the forked child. Do some setup on the new process.
      if (myProcShm == &allProcShm[i]) {
        myProcShm->waitIdx = plrShm->curWaitIdx;
        plrShm->condWaitCnt[waitIdx]++;
        plrShm->restoring = 0;
        printf("[%d] Replacement process started\n", myProcShm->pid);
      }
      
      break;
    }
  }
  
  // Handle error case of watchdog expiring but all processes waiting
  if (!didReplace) {
    fprintf(stderr, "[%d] Error: Watchdog expired but didn't find any non-waiting processes\n", myProcShm->pid);
    return -1;
  }
  
  // Return 1 to indicate that replacement processes should have been created
  return 1;
}

///////////////////////////////////////////////////////////////////////////////

int plr_forkNewProcess(perProcData_t *newProcShm) {
  int childPid = fork();
  if (childPid < 0) {
    perror("fork");
    exit(1);
  } else if (childPid) {
    // Parent just falls through
  } else {
    // Child doesn't hold lock when exiting fork, need to acquire lock
    // before doing anything else
    pthread_mutex_lock(&plrShm->lock);
  
    // Initialize this new proc's data area
    myProcShm = newProcShm;
    plrSD_initProcData(myProcShm);
  }
  
  return 0;
}

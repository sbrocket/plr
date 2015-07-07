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

int plr_wait() {
  if (insidePLRCode) {
    return 0;
  }
  insidePLRCode = 1;
  
  pthread_mutex_lock(&plrShm->lock);
  int myPid = myProcShm->pid;
  
  // Ignore calls to plr_wait that come from the wrong pid, seems to
  // occur when instrumenting binary with Pin
  if (myPid != getpid()) {
    pthread_mutex_unlock(&plrShm->lock);
    insidePLRCode = 0;
    return 0;
  }
  
  int waitIdx = plrShm->curWaitIdx;
  assert(plrShm->condWaitCnt[waitIdx] <= plrShm->nProc);
  
  // Mark this process as waiting
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
  while (myProcShm->waitIdx >= 0 && plrShm->condWaitCnt[waitIdx] < plrShm->nProc) {
    if (watchdogExpired && !plrShm->restoring) {
      // Watchdog timer expired, look for dead/stuck processes
      if (plrShm->nProc - plrShm->condWaitCnt[waitIdx] > 1) {
        // More than 1 process is not waiting, can't recover
        fprintf(stderr, "[%d] Error: Watchdog expired & more than 1 process is not waiting - unrecoverable\n", myPid);
        pthread_mutex_unlock(&plrShm->lock);
        exit(1);
      }
      for (int i = 0; i < plrShm->nProc; ++i) {
        if (allProcShm[i].waitIdx < 0) {
          printf("[%d] Pid %d failed to wait before watchdog expired\n", myPid, allProcShm[i].pid);
          plrShm->restoring = 1;
          
          // Kill stuck process (if not already dead)
          kill(allProcShm[i].pid, SIGKILL);
          if (plrSD_freeProcData(&allProcShm[i]) < 0) {
            fprintf(stderr, "[%d] plrSD_freeProcData failed\n", myPid);
          }
          
          // Fork replacement process from current good process
          if (plr_forkNewProcess(&allProcShm[i]) < 0) {
            fprintf(stderr, "[%d] plr_forkNewProcess failed\n", myPid);
          }

          // If myProcShm equals the area for the process that was just killed,
          // then this is the forked child. Do some setup on the new process.
          if (myProcShm == &allProcShm[i]) {
            myPid = myProcShm->pid;
            myProcShm->waitIdx = plrShm->curWaitIdx;
            plrShm->condWaitCnt[waitIdx]++;
            plrShm->restoring = 0;
            printf("[%d] Forked replacement process starting to wait\n", myPid);
          }
          
          break;
        }
      }
      // Should have forked new process to replace stuck one, check exit 
      // condition again
      continue;
    }
    
    // Must use CLOCK_REALTIME, _timedwait needs abstime since epoch
    struct timespec absWait;
    if (clock_gettime(CLOCK_REALTIME, &absWait) < 0) {
      perror("clock_gettime");
      pthread_mutex_unlock(&plrShm->lock);
      exit(1);
    }
    absWait = tspecAddMs(absWait, 100);
        
    // Using _timedwait as a watchdog timer, to avoid deadlock in case one of the
    // redundant processes has died or is stuck
    // NOTE: This ignores the case of pthread_cond_timedwait returning EINTR, in
    // which case less time than specified has elapsed. This can result in a longer
    // than desired wait time.

    int ret = pthread_cond_timedwait(&myProcShm->cond[waitIdx], &plrShm->lock, &absWait);
    if (ret == ETIMEDOUT) {
      // Loop again to make sure timer didn't expire while last proc was waiting
      watchdogExpired = 1; 
    } else if (ret != 0) {
      fprintf(stderr, "[%d] pthread_cond_timedwait returned %d\n", myPid, ret);
    }
  }
  
  // If this was the last process to wait, wake up all other processes
  if (myProcShm->waitIdx >= 0) {
    // Shift curWaitIdx to other value so subsequent plr_wait calls use the
    // other condition variable
    plrShm->curWaitIdx = (plrShm->curWaitIdx) ? 0 : 1;
    
    //printf("[%d] Signaling all processes to wake up for idx %d\n", myPid, waitIdx);
    for (int i = 0; i < plrShm->nProc; ++i) {
      pthread_cond_signal(&allProcShm[i].cond[waitIdx]);
      allProcShm[i].waitIdx = -1;
    }
  }
  // Decrement waiting process counter
  plrShm->condWaitCnt[waitIdx]--;
  
  pthread_mutex_unlock(&plrShm->lock);
  insidePLRCode = 0;
  return 0;
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

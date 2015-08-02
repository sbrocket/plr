#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <assert.h>
#include <signal.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <string.h>

#include "plr.h"
#include "plrLog.h"
#include "plrSharedData.h"
#include "plrCompare.h"
#include "timeUtil.h"
#include "pthreadUtil.h"

///////////////////////////////////////////////////////////////////////////////
// Global data

static int g_insidePLRInternal = 0;

///////////////////////////////////////////////////////////////////////////////
// Private functions

// Create a new redundant PLR process as a copy of the calling process.
// plrShm->lock shall be held while calling this, and is held by each process
// when it returns from this.
int plr_forkNewProcess(perProcData_t *newProcShm);

typedef enum {
  // Any process may perform the wait action
  WAIT_ACTION_ANY,
  // Only the master process may perform the wait action
  WAIT_ACTION_MASTER,
  // Only a slave process may perform the wait action
  WAIT_ACTION_SLAVE
} waitActionType_t;

// Process synchronization barrier
// Once processes have all reached the barrier, the provided function pointer
// is called in one process to perform some action. This function shall return
// the following values:
//   < 0 : Error occurred
//     0 : Action completed, let all processes exit barrier
//     1 : Rerun action on next sequential process
// The wait action type determines which process will perform the wait action.
int plr_waitBarrier(int (*actionPtr)(void), waitActionType_t actionType);

// Barrier action function for plr_checkSyscallArgs()
int plr_checkSyscallArgs_act();

// Handle expired watchdog timer during plr_waitBarrier
int plr_watchdogExpired();

// Replace the process at the given index (in terms of allProcShm) with a
// copy of the calling process.
// plrShm->lock shall be held while calling this, and is held by each process
// when it returns from this.
int plr_replaceProcessIdx(int idx);

///////////////////////////////////////////////////////////////////////////////

int plr_figureheadInit(int nProc, int pintoolMode) {
  // Set figurehead process as subreaper so grandchild processes
  // get reparented to it, instead of init
  if (prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) < 0) {
    plrlog(LOG_ERROR, "Error: prctl PR_SET_CHILD_SUBREAPER failed\n");
    return -1;
  }
  
  if (plrSD_initSharedData(nProc) < 0) {
    plrlog(LOG_ERROR, "Error: PLR Shared data init failed\n");
    return -1;
  }
  plrShm->insidePLRInitTrue = pintoolMode;
  return 0;
}

///////////////////////////////////////////////////////////////////////////////

int plr_figureheadExit() {
  if (plrSD_cleanupSharedData() < 0) {
    plrlog(LOG_ERROR, "Error: PLR shared data cleanup failed\n");
    return -1;
  }
  return 0;
}

///////////////////////////////////////////////////////////////////////////////

int plr_processInit() {
  g_insidePLRInternal = 1;
  
  if (plrSD_acquireSharedData() < 0) {
    plrlog(LOG_ERROR, "Error: PLR shared data acquire failed\n");
    return -1;
  }
  //plrlog(LOG_DEBUG, [%d] plr: &plrShm = %p, plrShm = %p\n", getpid(), &plrShm, plrShm);
  
  if (myProcShm) {
    plrlog(LOG_DEBUG, "[%d] Called init but already has myProcShm\n", getpid());
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
        plrlog(LOG_ERROR, "[%d] plr_forkNewProcess failed\n", myPid);
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
  
  g_insidePLRInternal = 0;
  if (plrShm->insidePLRInitTrue && !myProcShm->insidePLRSetOnce) {
    myProcShm->insidePLRSetOnce = 1;
    plr_setInsidePLR();
  }
  
  pthread_mutex_unlock(&plrShm->lock);
  
  return 0;
}

///////////////////////////////////////////////////////////////////////////////

int plr_checkSyscallArgs(const syscallArgs_t *args) {
  // Copy syscall arguments to shared memory area
  memcpy(&myProcShm->syscallArgs, args, sizeof(syscallArgs_t));
  
  // Wait for all processes to reach this barrier, then compare all syscall arguments
  if (plr_waitBarrier(&plr_checkSyscallArgs_act, WAIT_ACTION_ANY) < 0) {
    plrlog(LOG_ERROR, "Error: plr_waitBarrier failed\n");
    exit(1);
  }
  
  return 0;
}

///////////////////////////////////////////////////////////////////////////////

// Barrier action for plr_checkSyscallArgs()
int plr_checkSyscallArgs_act() {
  // TODO: Temporarily assuming 3 redundant processes
  assert(plrShm->nProc == 3);
  
  // Compare 1st & 2nd and 2nd & 3rd process syscall arguments
  int comp0vs1 = plrC_compareArgs(&allProcShm[0].syscallArgs, 
                                  &allProcShm[1].syscallArgs);
  int comp1vs2 = plrC_compareArgs(&allProcShm[1].syscallArgs, 
                                  &allProcShm[2].syscallArgs);
  
  // Check for faulted processes based on argument comparisons
  int badProc;
  if (comp0vs1 == 0 && comp1vs2 == 0) {
    // All arguments agree
    badProc = -1;
  } else {
    // Some arguments disagree
    int comp0vs2 = plrC_compareArgs(&allProcShm[0].syscallArgs,
                                    &allProcShm[2].syscallArgs);
                                    
    if (comp0vs1 > 0 && comp1vs2 == 0) {
      // Proc 0 disagrees with 1 & 2
      badProc = 0;
    } else if (comp0vs1 > 0 && comp0vs2 == 0) {
      // Proc 1 disagrees with 0 & 2
      badProc = 1;
    } else if (comp0vs2 > 0 && comp0vs1 == 0) {
      // Proc 2 disagrees with 0 & 1
      badProc = 2;
    } else {
      // All processes disagree
      badProc = plrShm->nProc;
    }
  }
  
  if (badProc == -1) {
    // All arguments agree, nothing to do
    return 0;
  } else if (badProc >= 0 && badProc < plrShm->nProc) {
    if (myProcShm == &allProcShm[badProc]) {
      // Detected current process as faulted, need to rerun action on
      // a different (good) process so it can be replaced
      plrlog(LOG_DEBUG, "[%d] Current process is bad!\n", getpid());
      return 1;
    } else {
      // Replace bad process with copy of current process
      plrlog(LOG_DEBUG, "[%d] Replacing faulted pid %d\n", getpid(), allProcShm[badProc].pid);
      if (plr_replaceProcessIdx(badProc) < 0) {
        plrlog(LOG_ERROR, "Error: plr_replaceProcessIdx failed\n");
        return -1;
      }
      return 0;
    }
  } else {
    // Multiple disagreements detected
    plrlog(LOG_DEBUG, "[%d] No processes agree with each other! Unrecoverable fault\n", getpid());
    return -1;
  }
}

///////////////////////////////////////////////////////////////////////////////

int plr_isMasterProcess() {
  // Whichever process is index 0 in allProcShm is treated as the 
  // "master process"
  if (myProcShm == &allProcShm[0]) {
    return 1;
  } else {
    return 0;
  }
}

///////////////////////////////////////////////////////////////////////////////

int plr_masterAction(int (*actionPtr)(void)) {
  // Wait for all processes to reach this barrier, then master process will
  // run the provided function
  if (plr_waitBarrier(actionPtr, WAIT_ACTION_MASTER) < 0) {
    plrlog(LOG_ERROR, "Error: plr_waitBarrier failed\n");
    exit(1);
  }
  
  return 0;
}

///////////////////////////////////////////////////////////////////////////////

int plr_copyToShm(const void *src, size_t length, size_t offset) {
  // First resize extraShm area so that it's at least as big as needed
  if (plrSD_resizeExtraShm(offset+length) < 0) {
    plrlog(LOG_ERROR, "[%d] Error: plrSD_resizeExtraShm failed\n", getpid());
    exit(1);
  }
  
  // Refresh this process's extraShm mapping
  if (plrSD_refreshExtraShm() < 0) {
    plrlog(LOG_ERROR, "[%d] Error: plrSD_refreshExtraShm failed\n", getpid());
    exit(1);
  }
  
  // Copy data into extraShm at specified offset
  memcpy(extraShm+offset, src, length);
  return 0;
}

///////////////////////////////////////////////////////////////////////////////

int plr_copyFromShm(void *dest, size_t length, size_t offset) {
  // Refresh this process's extraShm mapping
  if (plrSD_refreshExtraShm() < 0) {
    plrlog(LOG_ERROR, "[%d] Error: plrSD_refreshExtraShm failed\n", getpid());
    exit(1);
  }
  
  // Copy data from extraShm at specified offset
  memcpy(dest, extraShm+offset, length);
  return 0;
}

///////////////////////////////////////////////////////////////////////////////
void plr_setInsidePLR() {
  assert(myProcShm);
  assert(!myProcShm->insidePLR);
  myProcShm->insidePLR = 1;
}

///////////////////////////////////////////////////////////////////////////////

void plr_clearInsidePLR() {
  assert(myProcShm);
  assert(myProcShm->insidePLR);
  myProcShm->insidePLR = 0;
}

///////////////////////////////////////////////////////////////////////////////

int plr_checkInsidePLR() {
  assert(g_insidePLRInternal || myProcShm);
  return g_insidePLRInternal || myProcShm->insidePLR;
}

///////////////////////////////////////////////////////////////////////////////

int plr_waitBarrier(int (*actionPtr)(void), waitActionType_t actionType) {
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
      if (actionPtr) {
        int runAction = 0;
        
        switch (actionType) {
        case WAIT_ACTION_ANY:
          runAction = 1;
          break;
        case WAIT_ACTION_MASTER:
          if (plr_isMasterProcess()) {
            runAction = 1;
          } else {
            // Wake up master process so it can run action
            pthread_cond_signal(&allProcShm[0].cond[waitIdx]);
          }
          break;
        case WAIT_ACTION_SLAVE:
          if (plr_isMasterProcess()) {
            // Wake up a slave process so it can run action
            pthread_cond_signal(&allProcShm[1].cond[waitIdx]);
          } else {
            runAction = 1;
          }
        }
        
        if (runAction) {
          int ret = actionPtr();
          if (ret < 0) {
            myProcShm->waitIdx = -1;
            plrShm->condWaitCnt[waitIdx]--;
            pthread_mutex_unlock(&plrShm->lock);
            exit(1);
          } else if (ret == 0) {
            actionSuccess = 1;
          } else {
            // Signal another process to wake up & run action
            for (int i = 0; i < plrShm->nProc; ++i) {
              if (myProcShm != &allProcShm[i]) {
                pthread_cond_signal(&allProcShm[i].cond[waitIdx]);
                break;
              }
            }
          }
        }
      } else {
        // No action function ptr provided, default success once exit condition met
        actionSuccess = 1;
      }
      
      // If barrier action succeeds, wake up all other processes & leave barrier
      if (actionSuccess && myProcShm->waitIdx >= 0) {
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
      plrlog(LOG_ERROR, "[%d] pthread_cond_timedwait returned %d\n", getpid(), ret);
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
    plrlog(LOG_ERROR, "[%d] Error: Watchdog expired & more than 1 process is not waiting - unrecoverable\n", myProcShm->pid);
    return -1;
  }
  
  // Replace faulted process with a copy of the current process
  int didReplace = 0;
  for (int i = 0; i < plrShm->nProc; ++i) {
    if (allProcShm[i].waitIdx < 0) {
      plrlog(LOG_DEBUG, "[%d] Pid %d failed to wait before watchdog expired\n", myProcShm->pid, allProcShm[i].pid);
      plrShm->restoring = 1;
      didReplace = 1;
      
      // Replace non-waiting process with a copy of the current process
      // Note that both the current & new processes will exit this function
      if (plr_replaceProcessIdx(i) < 0) {
        plrlog(LOG_ERROR, "Error: plr_replaceProcessIdx failed\n");
        return -1;
      }

      // If myProcShm equals the area for the process that was just killed,
      // then this is the forked child. Do some setup on the new process.
      if (myProcShm == &allProcShm[i]) {
        myProcShm->waitIdx = plrShm->curWaitIdx;
        plrShm->condWaitCnt[waitIdx]++;
        plrShm->restoring = 0;
        plrlog(LOG_DEBUG, "[%d] Watchdog replacement process started\n", myProcShm->pid);
      }
      
      break;
    }
  }
  
  // Handle error case of watchdog expiring but all processes waiting
  if (!didReplace) {
    plrlog(LOG_ERROR, "[%d] Error: Watchdog expired but didn't find any non-waiting processes\n", myProcShm->pid);
    return -1;
  }
  
  // Return 1 to indicate that replacement processes should have been created
  return 1;
}

///////////////////////////////////////////////////////////////////////////////

int plr_replaceProcessIdx(int idx) {
  // Can't replace self
  assert(myProcShm != &allProcShm[idx]);
  
  // Kill faulted process
  kill(allProcShm[idx].pid, SIGKILL);
  if (plrSD_freeProcData(&allProcShm[idx]) < 0) {
    plrlog(LOG_ERROR, "[%d] plrSD_freeProcData failed\n", myProcShm->pid);
    return -1;
  }
  
  // Fork replacement process from current good process
  if (plr_forkNewProcess(&allProcShm[idx]) < 0) {
    plrlog(LOG_ERROR, "[%d] plr_forkNewProcess failed\n", myProcShm->pid);
    return -1;
  }
  
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
    perProcData_t *parentProcShm = myProcShm;
    myProcShm = newProcShm;
    plrSD_initProcDataAsCopy(myProcShm, parentProcShm);
  }
  
  return 0;
}

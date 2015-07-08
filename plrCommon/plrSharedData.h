#ifndef PLR_SHARED_DATA_H
#define PLR_SHARED_DATA_H
#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>
#include "plrCompare.h"

typedef struct {
  int pid;
  // Index of condition variable currently waiting in. Value of -1 indicates
  // not waiting, whereas 0 or 1 gives the index currently waiting in.
  int waitIdx;
  // Two separate condition variables used to keep track of separate wait events.
  pthread_cond_t cond[2];
  // Saved syscall arguments from last checked syscall
  syscallArgs_t syscallArgs;
} perProcData_t;

typedef struct {
  // Total number of redundant processes
  int nProc;
  // Index of current condition variable to wait in.
  int curWaitIdx;
  // Count of processes currently waiting for a given condition
  // variable index.
  int condWaitCnt[2];
  // Boolean flag, set true when in the middle of restoring a failed process.
  int restoring;
  pthread_mutex_t lock;
  
  // Fault injection pintool data
  // The following data is added here for convenience, to avoid creating a separate shared 
  // data region. It is only used by the fault injection pintool, not by PLR itself.
  pthread_mutex_t toolLock;
  int nextFaultIdx;
  int nextFaultPid;
  unsigned int randSeed;
} plrData_t;

extern plrData_t *plrShm;
// allProcShm is an array of all processes' per-proc data, 
// i.e. effectively "perProcData_t[plrShm->nProc]"
extern perProcData_t *allProcShm;
// myProcShm is a pointer to this process's per-proc data
extern perProcData_t *myProcShm;

// Initialize the shared data area for the first time.
int plrSD_initSharedData(int nProc);

// Map the existing shared data area and allocate an entry in
// allProcShm to the current process (as myProcShm).
int plrSD_acquireSharedData();

// Destroy the shared data area. No other processes should call
// plrSD_acquireSharedData after this, but existing in-memory
// mappings are not invalidated.
int plrSD_cleanupSharedData();

// Initializes the given per-proc data struct for the current process.
// plrShm->lock shall be held while calling this.
int plrSD_initProcData(perProcData_t *procShm);

// Initializes the given per-proc data struct for the current process,
// copying some data from the source's area and setting others for the
// new process.
// plrShm->lock shall be held while calling this.
int plrSD_initProcDataAsCopy(perProcData_t *procShm, perProcData_t *parent);

// Zeroes out the given per-proc data struct, readying it for reuse.
// plrShm->lock shall be held while calling this.
int plrSD_freeProcData(perProcData_t *procShm);

#ifdef __cplusplus
}
#endif
#endif

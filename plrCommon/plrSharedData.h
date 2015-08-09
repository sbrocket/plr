#ifndef PLR_SHARED_DATA_H
#define PLR_SHARED_DATA_H
#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>
#include "plrCompare.h"

typedef struct {
  int pid;
  // File descriptor of shared memory area
  int shmFd;
  // Index of condition variable currently waiting in. Value of -1 indicates
  // not waiting, whereas 0 or 1 gives the index currently waiting in.
  int waitIdx;
  // Two separate condition variables used to keep track of separate wait events.
  pthread_cond_t cond[2];
  // Saved syscall arguments from last checked syscall
  syscallArgs_t syscallArgs;
  // Current mapped size of extra shared memory area for this process
  int extraShmMapped;
  // Boolean flag, indicates that currently inside PLR code
  int insidePLR;
} perProcData_t;

typedef struct {
  // Figurehead process PID
  int figureheadPid;
  // Total number of redundant processes
  int nProc;
  // Watchdog timeout interval (in milliseconds)
  long watchdogTimeout;
  // Index of current condition variable to wait in.
  int curWaitIdx;
  // Count of processes currently waiting for a given condition
  // variable index.
  int condWaitCnt[2];
  // Boolean flag, set true when in the middle of restoring a failed process.
  int restoring;
  // Mutex lock used for shared across all PLR processes.
  pthread_mutex_t lock;
  // Current global size of extra shared memory area
  int extraShmSize;
  // Boolean flag, indicates that "insidePLR" flag should start out set
  int insidePLRInitTrue;
  // Boolean flag, indicates that process init has run once
  int didProcessInit;
  
  // Fault injection pintool data
  // The following data is added here for convenience, to avoid creating a separate shared 
  // data region. It is only used by the fault injection pintool, not by PLR itself.
  pthread_mutex_t toolLock;
  int faultInjected;
  int nextFaultIdx;
  int nextFaultPid;
  unsigned long eventCount;
  unsigned long targetCount;
} plrData_t;

extern plrData_t *plrShm;
// allProcShm is an array of all processes' per-proc data, 
// i.e. effectively "perProcData_t[plrShm->nProc]"
extern perProcData_t *allProcShm;
// myProcShm is a pointer to this process's per-proc data
extern perProcData_t *myProcShm;
// extraShm is a pointer to an area of shared memory that
// can change dynamically, as needed to copy syscall data
// between processes, throughout the life of a PLR process group
extern void *extraShm;

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
// Note that the condition variable will be broadcasted as part of this
// before being destroyed.
// plrShm->lock shall be held while calling this.
int plrSD_freeProcData(perProcData_t *procShm);

// Expand the global extra shared memory area to at least minSize bytes.
// plrShm->lock shall be held while calling this.
int plrSD_resizeExtraShm(int minSize);

// Refresh the extra shared memory area mapped for this process.
int plrSD_refreshExtraShm();

#ifdef __cplusplus
}
#endif
#endif

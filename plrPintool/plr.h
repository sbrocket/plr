#ifndef PLR_H
#define PLR_H
#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include "plrCompare.h"

// Update the global shared data variables (if needed)
void plr_refreshSharedData();

// plr_processInit() should only be called once, by the first redundant
// process started by the figurehead. It will acquire the shared data area
// and fork the other redundant processes.
int plr_processInit();

// Call to check syscall arguments between all redundant processes.
// If any disagreements or stuck processes (indicated by all but 1 process
// not entering this function) are found, the faulted process is killed and
// replaced with a copy of a good process. 
// 'nProc' good processes will always exit this function (within the limits
// of PLR's fault recovery capability), even if a faulted process enters.
int plr_checkSyscallArgs(const syscallArgs_t *args);

// Check whether the current process is the master process or not.
// Returns 1 if master, 0 if slave, and -1 on error.
int plr_isMasterProcess();

// Performs an action on the master process only after synchronizing all
// processes at a barrier. Note that plrShm->lock is held when the action
// function is called.
// The provided action function shall return <0 if an error occurs or
// 0 if it completes normally.
int plr_masterAction(int (*actionPtr)(void));

// These two functions are used to copy generic data into and out of an
// area of process shared memory, which is allocated transparently based
// on the offset and length arguments. Used for passing data between
// the processes inside overriden system call functions.
// plr_copyToShm() shall only be called when plrShm->lock is held,
// such as within a plr_masterAction's action.
int plr_copyToShm(const void *src, size_t length, size_t offset);
// The user is responsible for ensuring that plr_copyFromShm() is 
// synchronized with other processes properly to get the expected data,
// but plrShm->lock need not be held.
int plr_copyFromShm(void *dest, size_t length, size_t offset);

// These functions are used to manage a per-process flag indicating whether
// currently inside core PLR code. Used by the overriden system call 
// functions to avoid recursion.
void plr_setInsidePLR();
void plr_clearInsidePLR();
int plr_checkInsidePLR();

void plr_printInfo();

#ifdef __cplusplus
}
#endif
#endif

#ifndef PLR_H
#define PLR_H
#ifdef __cplusplus
extern "C" {
#endif

#include "plrCompare.h"

int plr_figureheadInit(int nProc);
int plr_figureheadExit();

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
int plr_checkSyscall(const syscallArgs_t *args);

#ifdef __cplusplus
}
#endif
#endif

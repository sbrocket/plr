#ifndef PLR_H
#define PLR_H
#ifdef __cplusplus
extern "C" {
#endif

int plr_figureheadInit(int nProc);
int plr_figureheadExit();

// plr_processInit() should only be called once, by the first redundant
// process started by the figurehead. It will acquire the shared data area
// and fork the other redundant processes.
int plr_processInit();

int plr_checkSyscall();

#ifdef __cplusplus
}
#endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>
#include <pthread.h>
#include "pin.H"
#include "plr.h"
#include "plrLog.h"
#include "plrSharedData.h"

///////////////////////////////////////////////////////////////////////////////
// Global variables
FILE *trace;
double faultProb;
perProcData_t *nextProcToFault = NULL;

///////////////////////////////////////////////////////////////////////////////
// Commandline switches
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
    "o", "plrTool.out", "Specify output file name");
KNOB<string> KnobFaultProb(KNOB_MODE_WRITEONCE, "pintool",
    "p", "0.01", "Specify per-instruction fault injection probability");
    
///////////////////////////////////////////////////////////////////////////////

static INT32 Usage() {
  PIN_ERROR("Pintool for injecting transient faults.\n"
            + KNOB_BASE::StringKnobSummary() + "\n");
  return -1;
}

///////////////////////////////////////////////////////////////////////////////

VOID getPLRShm() {
  // BANDAID: The pintool seems to access a different plrShm than the process it's
  // attached to. Until a way to access the process's own plrShm is found,
  // we can just acquire the shared data area again for this separate pointer.
  if (plrShm == NULL && plrSD_acquireSharedData() < 0) {
    plrlog(LOG_ERROR, "Error: plrSD_acquireSharedData() failed in pintool\n");
    exit(1);
  }
  if (plrShm == NULL) {
    plrlog(LOG_ERROR,  "[%d:pin] plrShm still NULL in pintool\n", PIN_GetPid());
    exit(1);
  }
  //printf("[%d] pin: &plrShm = %p, plrShm = %p\n", PIN_GetPid(), &plrShm, plrShm);

  // Set myProcShm properly inside Pintool
  int myPid = PIN_GetPid();
  if (myProcShm == NULL || myProcShm->pid != myPid) {
    for (int i = 0; i < plrShm->nProc; ++i) {
      if (allProcShm[i].pid == myPid) {
        myProcShm = &allProcShm[i];
        break;
      }
    }
    if (myProcShm == NULL) {
      plrlog(LOG_ERROR, "[%d:pin] myProcShm still NULL in pintool\n", PIN_GetPid());
      exit(1);
    }
  }
}

///////////////////////////////////////////////////////////////////////////////

VOID updateNextProcToFault(BOOL initialCall) {   
  getPLRShm();
 
  if (initialCall) {
    pthread_mutex_lock(&plrShm->toolLock);
  }
  
  // Initialize random seed
  int curFaultPid = plrShm->nextFaultPid;
  if (curFaultPid == 0) {
    plrShm->randSeed = time(NULL)+PIN_GetPid();
  }
  
  if (curFaultPid == 0 || !initialCall) {
    double roll = (double)rand_r(&plrShm->randSeed)/RAND_MAX;
    int idx = (int)(plrShm->nProc*roll) % plrShm->nProc;
    int nextPid = allProcShm[idx].pid;
    
    if (nextPid != curFaultPid && nextPid != 0) {
      plrShm->nextFaultPid = nextPid;
      plrShm->nextFaultIdx = idx;
      plrShm->processFaulted = 0;
      plrlog(LOG_DEBUG, "[%d:pin] Updated next proc to fault to pid %d (idx %d)\n", PIN_GetPid(), nextPid, idx);
    }
  }
  
  if (initialCall) {
    pthread_mutex_unlock(&plrShm->toolLock);
  }
}

///////////////////////////////////////////////////////////////////////////////

ADDRINT plrLoAddr, plrHiAddr;
VOID ImageLoad(IMG img, VOID *v) {
  // Insert a call after plr_processInit to initialize which process will
  // have a fault injected
  //printf("[%d] ImageLoad %s\n", PIN_GetPid(), IMG_Name(img).c_str());
  RTN rtn = RTN_FindByName(img, "plr_processInit");
  if (RTN_Valid(rtn)) {
    RTN_Open(rtn);
    RTN_InsertCall(rtn, IPOINT_AFTER, (AFUNPTR)updateNextProcToFault, IARG_BOOL, TRUE, IARG_END);
    RTN_Close(rtn);
  }
  
  // TODO: Check lo/hi vs start/size to see where different Pin plrShm & other global data is
  //printf("Lo 0x%lx, Hi 0x%lx, Start 0x%lx, Size 0x%x\n", IMG_LowAddress(img), IMG_HighAddress(img), IMG_StartAddress(img), IMG_SizeMapped(img));
  
  string imageName = IMG_Name(img);
  string targetLib = "libplrPreload.so";
  if (imageName.length() >= targetLib.length() 
      && imageName.substr(imageName.length()-targetLib.length()) == targetLib)
  {
    plrLoAddr = IMG_LowAddress(img);
    plrHiAddr = IMG_HighAddress(img);
  }
}

///////////////////////////////////////////////////////////////////////////////

VOID traceCallback() {
  // Only update next fault process after that process has failed and been
  // replaced
  if (plrShm->processFaulted == 1) {
    pthread_mutex_lock(&plrShm->toolLock);
    if (allProcShm[plrShm->nextFaultIdx].pid != plrShm->nextFaultPid) {
      updateNextProcToFault(FALSE);
    }
    pthread_mutex_unlock(&plrShm->toolLock);
  }
  
  // Make sure that myProcShm is pointing to the right area
  getPLRShm();
  
  // Exit if this isn't the process to be faulted
  if (plrShm->nextFaultPid != PIN_GetPid()) {
    return;
  }
  
  // Exit if this process is inside PLR code
  // Even though plrPreload traces aren't instruments, we could be inside another
  // library function called from PLR. This avoids injecting faults in that case.
  if (myProcShm->insidePLR) {
    return;
  }
  
  static long rollCount = 0;
  // Not bothering to lock on randSeed accesses, race condition here treated as additional "randomness"
  float roll = (float)rand_r(&plrShm->randSeed)/RAND_MAX;
  rollCount++;
  if (roll <= faultProb) {
    plrShm->processFaulted = 1;
    plrlog(LOG_DEBUG, "[%d:pin] Pintool killing process, %f <= %f, after %ld rolls\n", PIN_GetPid(), roll, faultProb, rollCount);
    PIN_ExitApplication(2);
  }
}

///////////////////////////////////////////////////////////////////////////////

VOID InstrumentTrace(TRACE trace, VOID *arg) {
  // plrLoAddr/plrHiAddr used to avoid instrumenting traces from plrPreload library
  ADDRINT addr = TRACE_Address(trace);
  if (TRACE_Original(trace) && (addr < plrLoAddr || addr > plrHiAddr)) {
    TRACE_InsertCall(trace, IPOINT_BEFORE, (AFUNPTR)traceCallback, IARG_END);
  }
}

///////////////////////////////////////////////////////////////////////////////

VOID ApplicationStart(VOID *arg) {
  // The "inside PLR" flag is initialized to true during PLR startup if running with the
  // fault injection Pintool, so that syscalls related to Pin startup aren't emulated.
  // This clears that initial flag.
  getPLRShm();
  plr_clearInsidePLR();
}

///////////////////////////////////////////////////////////////////////////////

void Fini(INT32 code, void *v) {
  //fclose(trace);
}

///////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[]) {
  // PIN_InitSymbols is required to search RTNs by name in ImageLoad
  PIN_InitSymbols();
  // Initialize Pin
  if(PIN_Init(argc,argv)) {
    return Usage();
  }
  
  // Parse fault injection probability option
  char *endptr;
  const char *faultProbStr = KnobFaultProb.Value().c_str();
  faultProb = strtod(faultProbStr, &endptr);
  if (endptr == faultProbStr || *endptr != '\0' || (faultProb == HUGE_VAL && errno == ERANGE)) {
    plrlog(LOG_ERROR, "Error: Argument for -p is not an float value\n");
    PIN_ExitApplication(1);
  }
  
  //trace = fopen(KnobOutputFile.Value().c_str(), "w");
  
  PIN_AddFiniFunction(Fini, NULL);
  IMG_AddInstrumentFunction(ImageLoad, NULL);
  TRACE_AddInstrumentFunction(InstrumentTrace, NULL);
  PIN_AddApplicationStartFunction(ApplicationStart, NULL);
  
  // Start the program, never returns
  PIN_StartProgram();
  
  return 0;
}

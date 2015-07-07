#include "pin.H"
#include "plrSharedData.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>
#include <pthread.h>

//-----------------------------------------------------------------------------
// Global variables
//-----------------------------------------------------------------------------
FILE *trace;
double faultProb;
perProcData_t *nextProcToFault = NULL;

//-----------------------------------------------------------------------------
// Commandline switches
//-----------------------------------------------------------------------------
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
    "o", "plrTool.out", "Specify output file name");
KNOB<string> KnobFaultProb(KNOB_MODE_WRITEONCE, "pintool",
    "p", "0.01", "Specify per-instruction fault injection probability");
    
//-----------------------------------------------------------------------------
// Usage() function
//-----------------------------------------------------------------------------
static INT32 Usage() {
  PIN_ERROR("Pintool for injecting transient faults.\n"
            + KNOB_BASE::StringKnobSummary() + "\n");
  return -1;
}

VOID updateNextProcToFault(BOOL initialCall) {
  // BANDAID: The pintool seems to access a different plrShm than the process it's
  // attached to. Until a way to access the process's own plrShm is found,
  // we can just acquire the shared data area again for this separate pointer.
  if (plrShm == NULL && plrSD_acquireSharedData() < 0) {
    fprintf(stderr, "Error: plrSD_acquireSharedData() failed in pintool\n");
    exit(1);
  }  
  if (plrShm == NULL) {
    fprintf(stderr, "[%d:pin] plrShm still NULL in pintool\n", PIN_GetPid());
    exit(1);
  }
  //printf("[%d] pin: &plrShm = %p, plrShm = %p\n", PIN_GetPid(), &plrShm, plrShm);
    
  if (initialCall) {
    pthread_mutex_lock(&plrShm->toolLock);
  }
  
  // Initialize random seed
  if (plrShm->nextFaultPid == 0) {
    plrShm->randSeed = time(NULL)+PIN_GetPid();
  }
  
  // Only update nextFaultPid if this is the current process to fault (or none is set yet)
  if (plrShm->nextFaultPid == 0 || !initialCall) {
    double roll = (double)rand_r(&plrShm->randSeed)/RAND_MAX;
    int idx = (int)(plrShm->nProc*roll) % plrShm->nProc;
    int nextPid = allProcShm[idx].pid;
    
    plrShm->nextFaultPid = nextPid;
    plrShm->nextFaultIdx = idx;
    printf("[%d:pin] Updated next proc to fault to pid %d (idx %d)\n", PIN_GetPid(), nextPid, idx);
  }
  
  if (initialCall) {
    pthread_mutex_unlock(&plrShm->toolLock);
  }
}

ADDRINT mainLoAddr, mainHiAddr;
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
  
  IMG_TYPE imgType = IMG_Type(img);
  if (imgType == IMG_TYPE_STATIC || imgType == IMG_TYPE_SHARED) {
    if (mainHiAddr != 0) {
      printf("[%d:pin] ERROR: pinFaultInject found 2 static/shared images\n", PIN_GetPid());
      exit(1);
    }
    mainLoAddr = IMG_LowAddress(img);
    mainHiAddr = IMG_HighAddress(img);
  }
}

VOID traceCallback() {
  // Only update next fault process after that process has failed and been
  // replaced
  pthread_mutex_lock(&plrShm->toolLock);
  if (allProcShm[plrShm->nextFaultIdx].pid != plrShm->nextFaultPid) {
    //printf("[%d:pin] Updating b/c allProcShm[%d].pid == %d != %d\n", PIN_GetPid(), plrShm->nextFaultIdx, allProcShm[plrShm->nextFaultIdx].pid, plrShm->nextFaultPid);
    updateNextProcToFault(FALSE);
  }
  pthread_mutex_unlock(&plrShm->toolLock);
  
  // Exit if this isn't the process to be faulted
  if (plrShm == NULL || plrShm->nextFaultPid != PIN_GetPid()) {
    return;
  }
  
  static long rollCount = 0;
  // Not bothering to lock on randSeed accesses, race condition here treated as additional "randomness"
  float roll = (float)rand_r(&plrShm->randSeed)/RAND_MAX;
  rollCount++;
  if (roll <= faultProb) {
    printf("[%d:pin] Pintool killing process, %f <= %f, after %ld rolls\n", PIN_GetPid(), roll, faultProb, rollCount);
    PIN_ExitApplication(2);
  }
}

VOID InstrumentTrace(TRACE trace, VOID *arg) {
  // Only instrument traces from main program, not from PLR code or other shared libs
  // TODO: Could instrument normal (e.g. libc) shared libs, just avoid injecting faults in PLR code
  ADDRINT addr = TRACE_Address(trace);
  if (TRACE_Original(trace) && addr >= mainLoAddr && addr <= mainHiAddr) {
    TRACE_InsertCall(trace, IPOINT_BEFORE, (AFUNPTR)traceCallback, IARG_END);
  }
}

VOID ApplicationStart(VOID *arg) {

}

//-----------------------------------------------------------------------------
// Fini() function
//-----------------------------------------------------------------------------
void Fini(INT32 code, void *v) {
  //fclose(trace);
}

//-----------------------------------------------------------------------------
// main() function
//-----------------------------------------------------------------------------
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
    fprintf(stderr, "Error: Argument for -p is not an float value\n");
    PIN_ExitApplication(1);
  }
  
  //trace = fopen(KnobOutputFile.Value().c_str(), "w");
  
  // Register Fini to be called when the application exits
  PIN_AddFiniFunction(Fini, NULL);
  
  // Register ImageLoad to be called when an image is loaded
  IMG_AddInstrumentFunction(ImageLoad, NULL);
  
  TRACE_AddInstrumentFunction(InstrumentTrace, NULL);
  
  //PIN_AddApplicationStartFunction(ApplicationStart, NULL);
  
  // Start the program, never returns
  PIN_StartProgram();
  
  return 0;
}

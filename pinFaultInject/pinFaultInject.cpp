#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>
#include <pthread.h>
#include <assert.h>
#include <chrono>
#include <random>
#include "pin.H"
#include "plr.h"
#include "plrLog.h"
#include "plrSharedData.h"

///////////////////////////////////////////////////////////////////////////////
// Global variables
perProcData_t *nextProcToFault = NULL;
ADDRINT plrLoAddr, plrHiAddr;
std::default_random_engine g_randGen;
std::normal_distribution<double> g_eventDist;
std::uniform_int_distribution<int> g_procDist;

///////////////////////////////////////////////////////////////////////////////
// Commandline switches
KNOB<BOOL> KnobTraceFaultMode(KNOB_MODE_WRITEONCE, "pintool",
    "trace", "0", "Enable trace fault injection mode");
KNOB<long> KnobTraceEventMean(KNOB_MODE_WRITEONCE, "pintool",
    "traceMean", "500", "Set mean # of traces before fault injection");
KNOB<BOOL> KnobInstrFaultMode(KNOB_MODE_WRITEONCE, "pintool",
    "ins", "0", "Enable instruction fault injection mode");
KNOB<long> KnobInstrEventMean(KNOB_MODE_WRITEONCE, "pintool",
    "insMean", "20000", "Set mean # of instructions before fault injection");
    
///////////////////////////////////////////////////////////////////////////////

static INT32 Usage() {
  PIN_ERROR("Pintool for injecting transient faults.\n"
            + KNOB_BASE::StringKnobSummary() + "\n");
  return -1;
}

///////////////////////////////////////////////////////////////////////////////

VOID updateNextProcToFault(BOOL initialCall) {
  plr_refreshSharedData();
 
  if (initialCall) {
    pthread_mutex_lock(&plrShm->toolLock);
  }
  
  // Choose the next PID to inject a fault into
  int curFaultPid = plrShm->nextFaultPid;
  if (curFaultPid == 0 || !initialCall) {
    int idx = g_procDist(g_randGen);
    int nextPid = allProcShm[idx].pid;
    
    if (nextPid != curFaultPid && nextPid != 0) {
      plrShm->nextFaultPid = nextPid;
      plrShm->nextFaultIdx = idx;
      plrShm->faultInjected = 0;
      plrlog(LOG_DEBUG, "[%d:pin] Updated next proc to fault to pid %d (idx %d)\n", PIN_GetPid(), nextPid, idx);
    }
  }
  
  // Choose the event count for the next fault injection
  plrShm->eventCount = 0;
  plrShm->targetCount = g_eventDist(g_randGen);
  
  if (initialCall) {
    pthread_mutex_unlock(&plrShm->toolLock);
  }
}

///////////////////////////////////////////////////////////////////////////////

BOOL shouldInjectFault() {
  // Only update next fault process after that process has failed and been
  // replaced
  if (plrShm->faultInjected == 1) {
    pthread_mutex_lock(&plrShm->toolLock);
    if (allProcShm[plrShm->nextFaultIdx].pid != plrShm->nextFaultPid) {
      updateNextProcToFault(FALSE);
    }
    pthread_mutex_unlock(&plrShm->toolLock);
  }
  
  // Exit if this isn't the process to be faulted
  if (plrShm->nextFaultPid != PIN_GetPid()) {
    return false;
  }
  
  // Make sure that myProcShm is pointing to the right area
  plr_refreshSharedData();
  
  // Exit if this process is inside PLR code
  // Even though plrPreload traces aren't instruments, we could be inside another
  // library function called from PLR. This avoids injecting faults in that case.
  if (myProcShm->insidePLR) {
    return false;
  }
  
  // Increment event count
  plrShm->eventCount++;
  //if (plrShm->eventCount % 1000 == 0) {
  //  plrlog(LOG_DEBUG, "[%d:pin] eventCount = %ld\n", PIN_GetPid(), plrShm->eventCount);
  //}
  if (plrShm->targetCount > 0 && plrShm->eventCount >= plrShm->targetCount) {
    return true;
  } else {
    return false;
  }
}

///////////////////////////////////////////////////////////////////////////////

VOID injectFault_trace() {
  if (shouldInjectFault()) {
    plrShm->faultInjected = 1;
    plrlog(LOG_DEBUG, "[%d:pin] Pintool killing process after %ld events\n", PIN_GetPid(), plrShm->eventCount);
    PIN_ExitApplication(2);
  }
}

///////////////////////////////////////////////////////////////////////////////

// Set of registers to choose from when injecting faults
static const REG faultRegSet[] = {
  REG_GDI, REG_GSI, REG_GBP, REG_STACK_PTR,
  REG_GBX, REG_GDX, REG_GCX, REG_GAX,
#if defined(TARGET_IA32E) || defined(TARGET_MIC)
  REG_R8,  REG_R9,  REG_R10, REG_R11,
  REG_R12, REG_R13, REG_R14, REG_R15,
#endif
  REG_INST_PTR
};

VOID injectFault_ins(const CONTEXT *cCtxt) {
  if (shouldInjectFault()) {
    plrShm->faultInjected = 1;
    plrlog(LOG_DEBUG, "[%d:pin] Pintool injecting fault after %ld events\n", PIN_GetPid(), plrShm->eventCount);
    
    // Update event counters to keep injecting faults into same process until it fails
    plrShm->eventCount = 0;
    plrShm->targetCount = g_eventDist(g_randGen);
    
    // Choose a register to inject fault into
    static std::uniform_int_distribution<int> g_regDist(0, sizeof(faultRegSet)/sizeof(*faultRegSet)-1);
    REG regToFault = faultRegSet[g_regDist(g_randGen)];
    
    // Choose a bit in the register to flip
    assert(REG_Width(regToFault) == REGWIDTH_64);
    static std::uniform_int_distribution<int> g_bitDist(0, 63);
    int bitToFlip = g_bitDist(g_randGen);
    
    // Flip the bit in the context and execute at new context state
    CONTEXT ctxt;
    PIN_SaveContext(cCtxt, &ctxt);
    ADDRINT regValue = PIN_GetContextReg(&ctxt, regToFault);
    regValue ^= 1 << bitToFlip;
    PIN_SetContextReg(&ctxt, regToFault, regValue);
    PIN_ExecuteAt(&ctxt);
  }
}

///////////////////////////////////////////////////////////////////////////////

VOID ImageLoad(IMG img, VOID *v) {
  // Insert a call after plr_processInit to initialize which process will
  // have a fault injected
  RTN rtn = RTN_FindByName(img, "plr_processInit");
  if (RTN_Valid(rtn)) {
    RTN_Open(rtn);
    RTN_InsertCall(rtn, IPOINT_AFTER, (AFUNPTR)updateNextProcToFault, IARG_BOOL, TRUE, IARG_END);
    RTN_Close(rtn);
  }

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

VOID InstrumentInstr(INS ins, VOID *arg) {
  // plrLoAddr/plrHiAddr used to avoid instrumenting instructions from plrPreload library
  ADDRINT addr = INS_Address(ins);
  if (INS_IsOriginal(ins) && (addr < plrLoAddr || addr > plrHiAddr)) {
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)injectFault_ins, IARG_CONST_CONTEXT, IARG_END);
  }
}

///////////////////////////////////////////////////////////////////////////////

VOID InstrumentTrace(TRACE trace, VOID *arg) {
  // plrLoAddr/plrHiAddr used to avoid instrumenting traces from plrPreload library
  ADDRINT addr = TRACE_Address(trace);
  if (TRACE_Original(trace) && (addr < plrLoAddr || addr > plrHiAddr)) {
    TRACE_InsertCall(trace, IPOINT_BEFORE, (AFUNPTR)injectFault_trace, IARG_END);
  }
}

///////////////////////////////////////////////////////////////////////////////

VOID ApplicationStart(VOID *arg) {
  // The "inside PLR" flag is initialized to true during PLR startup if running with the
  // fault injection Pintool, so that syscalls related to Pin startup aren't emulated.
  // This clears that initial flag.
  plr_refreshSharedData();
  plr_clearInsidePLR();
  
  g_procDist = std::uniform_int_distribution<int>(0, plrShm->nProc-1);
}

///////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[]) {
  // PIN_InitSymbols is required to search RTNs by name in ImageLoad
  PIN_InitSymbols();
  // Initialize Pin
  if(PIN_Init(argc,argv)) {
    return Usage();
  }
  
  // Initialize random number generator/distributions
  unsigned seed = std::chrono::system_clock::now().time_since_epoch().count() + PIN_GetPid();
  g_randGen = std::default_random_engine(seed);
  if (KnobTraceFaultMode) {
    long mean = KnobTraceEventMean.Value(), stdDev = mean/5;
    g_eventDist = std::normal_distribution<double>(mean,stdDev);
    
    TRACE_AddInstrumentFunction(InstrumentTrace, NULL);
  } else if (KnobInstrFaultMode) {
    long mean = KnobInstrEventMean.Value(), stdDev = mean/5;
    g_eventDist = std::normal_distribution<double>(mean,stdDev);

    INS_AddInstrumentFunction(InstrumentInstr, NULL);
  } else {
    plrlog(LOG_ERROR, "Error: No fault injection mode selected\n");
    PIN_ExitApplication(1);
  }
  
  IMG_AddInstrumentFunction(ImageLoad, NULL);
  PIN_AddApplicationStartFunction(ApplicationStart, NULL);
  
  // Start the program, never returns
  PIN_StartProgram();
  
  return 0;
}

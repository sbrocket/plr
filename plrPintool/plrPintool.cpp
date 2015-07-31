#include <stdio.h>
#include <stdlib.h>

#include "pin.H"
#include "plr.h"
#include "syscallRepl.h"

///////////////////////////////////////////////////////////////////////////////
// Global variables
FILE *trace;

///////////////////////////////////////////////////////////////////////////////
// Commandline switches
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
    "o", "plrTool.out", "Specify output file name");
    
///////////////////////////////////////////////////////////////////////////////

static INT32 Usage() {
  PIN_ERROR("Pintool for PLR's Syscall Emulation Unit.\n"
            + KNOB_BASE::StringKnobSummary() + "\n");
  return -1;
}


///////////////////////////////////////////////////////////////////////////////

VOID ApplicationStart(VOID *arg) {
  // The "inside PLR" flag is initialized to true during PLR startup if running with the
  // fault injection Pintool, so that syscalls related to Pin startup aren't emulated.
  // This clears that initial flag.
  printf("[%d] ApplicationStart\n", PIN_GetPid());
  
  if (plr_processInit() < 0) {
    fprintf(stderr, "Error: PLR process init failed\n");
    exit(1);
  }
  
  plr_refreshSharedData();
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
  
  //trace = fopen(KnobOutputFile.Value().c_str(), "w");
    
  PIN_AddFiniFunction(Fini, NULL);
  PIN_AddApplicationStartFunction(ApplicationStart, NULL);
  
  syscallRepl_main();
  
  // Start the program, never returns
  PIN_StartProgram();
  
  return 0;
}

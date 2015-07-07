#include "pin.H"
#include "plrSharedData.h"
#include <stdio.h>
#include <stdlib.h>

//-----------------------------------------------------------------------------
// Global variables
//-----------------------------------------------------------------------------
FILE *trace;

//-----------------------------------------------------------------------------
// Commandline switches
//-----------------------------------------------------------------------------
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
    "o", "plrTool.out", "Specify output file name");
    
//-----------------------------------------------------------------------------
// Usage() function
//-----------------------------------------------------------------------------
static INT32 Usage() {
  PIN_ERROR("Pintool for PLR's Syscall Emulation Unit.\n"
            + KNOB_BASE::StringKnobSummary() + "\n");
  return -1;
}

VOID ApplicationStart(VOID *arg) {
  if (plrSD_acquireSharedData() < 0) {
    fprintf(stderr, "Error: Failed to acquire PLR shared data area\n");
    exit(1);
  }
}
  
//-----------------------------------------------------------------------------
// Fini() function
//-----------------------------------------------------------------------------
void Fini(INT32 code, void *v) {
  fclose(trace);
}

//-----------------------------------------------------------------------------
// main() function
//-----------------------------------------------------------------------------
int main(int argc, char *argv[]) {
  // Initialize pin
  if(PIN_Init(argc,argv)) {
    return Usage();
  }
  
  trace = fopen(KnobOutputFile.Value().c_str(), "w");
  
  fprintf(trace, "[plrTool] Started on pid %d\n", PIN_GetPid());
  
  // Register Fini to be called when the application exits
  PIN_AddFiniFunction(Fini, NULL);
  
  PIN_AddApplicationStartFunction(ApplicationStart, NULL);
  
  // Start the program, never returns
  PIN_StartProgram();
  
  return 0;
}

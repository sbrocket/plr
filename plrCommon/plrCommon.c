#include <stdio.h>
#include <sys/prctl.h>

#include "plrCommon.h"
#include "plrSharedData.h"

///////////////////////////////////////////////////////////////////////////////

int plr_figureheadInit(int nProc, int pintoolMode, int pid) {
  // Set figurehead process as subreaper so grandchild processes
  // get reparented to it, instead of init
  if (prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) < 0) {
    fprintf(stderr, "Error: prctl PR_SET_CHILD_SUBREAPER failed\n");
    return -1;
  }
  
  if (plrSD_initSharedData(nProc) < 0) {
    fprintf(stderr, "Error: PLR Shared data init failed\n");
    return -1;
  }
  plrShm->figureheadPid = pid;
  plrShm->insidePLRInitTrue = pintoolMode;
  return 0;
}

///////////////////////////////////////////////////////////////////////////////

int plr_figureheadExit() {
  if (plrSD_cleanupSharedData() < 0) {
    fprintf(stderr, "Error: PLR shared data cleanup failed\n");
    return -1;
  }
  return 0;
}

#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include "plr.h"
#include "plrLog.h"
#include "plrSharedData.h"
#include "libc_func.h"

#include <stdio.h>

pid_t getpid() {
  // Get libc syscall function pointer & offset in image
  libc_func(getpid, pid_t);
  
  if (plr_checkInsidePLR()) {
    // If already inside PLR code, just call original syscall & return
    return _getpid();
  } else {
    plr_setInsidePLR();
    
    // Return figurehead PID
    pid_t ret = plrShm->figureheadPid;
    
    plrlog(LOG_SYSCALL, "[%d:getpid] Returning figurehead PID %d\n", _getpid(), ret);
    
    syscallArgs_t args = {
      .addr = _off_getpid,
    };
    plr_checkSyscallArgs(&args);
    
    plr_clearInsidePLR();
    return ret;
  }
}

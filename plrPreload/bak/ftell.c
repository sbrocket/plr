#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include "plr.h"
#include "plrLog.h"
#include "plrSharedData.h"
#include "libc_func.h"

long ftell(FILE *stream) {
  // Get libc syscall function pointer & offset in image
  libc_func(ftell, long, FILE *);
  
  if (plr_checkInsidePLR()) {
    // If already inside PLR code, just call original syscall & return
    return _ftell(stream);
  } else {
    plr_setInsidePLR();
    int fn = fileno(stream);
    plrlog(LOG_SYSCALL, "[%d:ftell] Fileno %d\n", getpid(), fn);
    
    syscallArgs_t args = {
      .addr = _off_ftell,
      .arg[0] = fn
    };
    plr_checkSyscallArgs(&args);
    
    // Call original libc function
    long ret = _ftell(stream);
    
    plr_clearInsidePLR();
    return ret;
  }
}

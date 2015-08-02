#include <unistd.h>
#include <errno.h>
#include "plr.h"
#include "plrLog.h"
#include "libc_func.h"

#include <stdio.h>

int close(int fd) {
  // Get libc syscall function pointer & offset in image
  libc_func(close, int, int);
    
  int ret;
  if (plr_checkInsidePLR()) {
    // If already inside PLR code, just call original syscall & return
    ret = _close(fd);
  } else {
    plr_setInsidePLR();
    plrlog(LOG_SYSCALL, "[%d:close] Close fd %d\n", getpid(), fd);
    
    syscallArgs_t args = {
      .addr = _off_close,
      .arg[0] = fd,
    };
    plr_checkSyscallArgs(&args);
    
    // Call original libc function
    ret = _close(fd);
    
    plr_clearInsidePLR();
  }
  
  return ret;
}

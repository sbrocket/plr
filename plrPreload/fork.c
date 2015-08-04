#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include "plr.h"
#include "plrLog.h"
#include "libc_func.h"

#include <stdio.h>

pid_t fork() {
  // Get libc syscall function pointer & offset in image
  libc_func(fork, pid_t);
  
  if (plr_checkInsidePLR()) {
    // If already inside PLR code, just call original syscall & return
    return _fork();
  } else {
    plr_setInsidePLR();
    
    plrlog(LOG_ERROR, "[%d:fork] ERROR: Target application called fork(), not supported by PLR\n", getpid());
    exit(1);
    
    plr_clearInsidePLR();
    return 0;
  }
}

pid_t vfork() {
  // Get libc syscall function pointer & offset in image
  libc_func(vfork, pid_t);
  
  if (plr_checkInsidePLR()) {
    // If already inside PLR code, just call original syscall & return
    return _vfork();
  } else {
    plr_setInsidePLR();
    
    plrlog(LOG_ERROR, "[%d:vfork] ERROR: Target application called vfork(), not supported by PLR\n", getpid());
    exit(1);
    
    plr_clearInsidePLR();
    return 0;
  }
}

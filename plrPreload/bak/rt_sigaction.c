#include <signal.h>
#include "plr.h"
#include "libc_func.h"

#include <stdio.h>
#include <unistd.h>

int rt_sigaction(int signum, const struct sigaction *act, struct sigaction *oldact) {
  // Get libc syscall function pointer & offset in image
  libc_func(rt_sigaction, int, int, const struct sigaction *, struct sigaction *);
    
  int ret;
  if (!plr_checkInsidePLR()) {
    plr_setInsidePLR();
    printf("[%d:rt_sigaction] signum %d\n", getpid(), signum);
    
    syscallArgs_t args = {
      .addr = _off_rt_sigaction,
      .arg[0] = signum,
    };
    plr_checkSyscallArgs(&args);
    
    // Call original libc function
    ret = _rt_sigaction(signum, act, oldact);
    
    plr_clearInsidePLR();
  } else {
    // If already inside PLR code, just call original syscall & return
    ret = _rt_sigaction(signum, act, oldact);
  }
  
  return ret;
}

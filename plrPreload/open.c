#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include "plr.h"
#include "libc_func.h"
#include "crc32_util.h"

#include <stdio.h>
#include <unistd.h>

// Common function to check syscall arguments & call libc for both open() and
// open64(), since they're otherwise identical
int commonOpen(const char *fncName, const char *pathname, int flags, va_list argl) {
  // Get libc syscall function pointer & offset in image
  libc_func_2(open, fncName, int, const char *, int, ...);
  
  mode_t mode = 0;
  if (flags & O_CREAT) {
    mode = va_arg(argl, mode_t);
    va_end(argl);
  }
  
  // If already inside PLR code, just call original syscall & return
  int setInside = 0;
  if (!plr_checkInsidePLR()) {
    setInside = 1;
    plr_setInsidePLR();
    printf("[%d:%s] Open file '%s'\n", getpid(), fncName, pathname);
    
    syscallArgs_t args = {
      .addr = _off_open,
      .arg[0] = crc32(0, pathname, strlen(pathname)),
      .arg[1] = flags,
      .arg[2] = mode
    };
    plr_checkSyscallArgs(&args);
  }
  
  // Call original libc function
  int ret;
  if (flags & O_CREAT) {
    ret = _open(pathname, flags, mode);
  } else {
    ret = _open(pathname, flags);
  }
  
  if (setInside) {
    plr_clearInsidePLR();
  }
  return ret;
}

int open(const char *pathname, int flags, ...) {
  va_list argl;
  va_start(argl, flags);
  int ret = commonOpen("open", pathname, flags, argl);
  va_end(argl);
  return ret;
}

int open64(const char *pathname, int flags, ...) {
  va_list argl;
  va_start(argl, flags);
  int ret = commonOpen("open64", pathname, flags, argl);
  va_end(argl);
  return ret;
}

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include "libc_func.h"
#include "plr.h"
#include "crc32c_util.h"

#include <stdio.h>
#include <unistd.h>

// Common function to check syscall arguments & call libc for both open() and
// open64(), since they're otherwise identical
int commonOpen(const char *fncName, const char *pathname, int flags, va_list argl) {
  printf("[%d:%s] Open file '%s'\n", getpid(), fncName, pathname);
  
  // Get libc syscall function pointer & offset in image
  libc_func_2(open, fncName, int, const char *, int, ...);
  
  syscallArgs_t args = {
    .addr = _off_open,
    .arg[0] = crc32(0, pathname, strlen(pathname)),
    .arg[1] = flags
  };
  
  mode_t mode;
  if (flags & O_CREAT) {
    mode = va_arg(argl, mode_t);
    args.arg[2] = mode;
    va_end(argl);
  }
  
  plr_checkSyscall(&args);
  
  // Call original libc function
  if (flags & O_CREAT) {
    return _open(pathname, flags, mode);
  } else {
    return _open(pathname, flags);
  }
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

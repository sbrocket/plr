#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include "libc_func.h"
#include "plr.h"

#include <unistd.h>

int open(const char *pathname, int flags, ...) {
  printf("[%d:open] Open file '%s'\n", getpid(), pathname);

  plr_wait();
  
  // Call original libc function
  libc_func(open, int, const char *, int, ...);
  //printf("[%d:open] %p, %p\n", getpid(), _open, _off_open);
  if (flags & O_CREAT) {
    va_list argl;
    va_start(argl, flags);
    mode_t mode = va_arg(argl, mode_t);
    va_end(argl);
    return _open(pathname, flags, mode);
  } else {
    return _open(pathname, flags);
  }
}

int open64(const char *pathname, int flags, ...) {
  printf("[%d:open64] Open file '%s'\n", getpid(), pathname);
  
  plr_wait();
  
  // Call original libc function
  libc_func(open64, int, const char *, int, ...);
  if (flags & O_CREAT) {
    va_list argl;
    va_start(argl, flags);
    mode_t mode = va_arg(argl, mode_t);
    va_end(argl);
    return _open64(pathname, flags, mode);
  } else {
    return _open64(pathname, flags);
  }
}


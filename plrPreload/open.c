#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "plr.h"
#include "plrLog.h"
#include "libc_func.h"
#include "crc32_util.h"

typedef struct {
  int err;
  int ret;
} openShmData_t;

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
  if (plr_checkInsidePLR()) {
    // Call original libc function
    int ret;
    if (flags & O_CREAT) {
      ret = _open(pathname, flags, mode);
    } else {
      ret = _open(pathname, flags);
    }
    return ret;
  } else {
    plr_setInsidePLR();
    plrlog(LOG_SYSCALL, "[%d:%s] Open file '%s'\n", getpid(), fncName, pathname);
    
    syscallArgs_t args = {
      .addr = _off_open,
      .arg[0] = crc32(0, pathname, strlen(pathname)),
      .arg[1] = flags,
      .arg[2] = mode
    };
    plr_checkSyscallArgs(&args);
    
    // Nested function actually performed by master process only
    // If O_EXCL specified in flags, master process creates file (or errors out)
    int ret;
    int masterAct() {
      // Hacky workaround...this makes sure the fd for the extraShm area
      // is open before calling open on other files, to prevent master & slave
      // fd's getting out of sync
      plr_copyToShm(NULL, 0, 0);
      
      // Call original libc function
      if (flags & O_CREAT) {
        ret = _open(pathname, flags, mode);
      } else {
        ret = _open(pathname, flags);
      }
      
      // Store return value in shared memory for slave processes
      openShmData_t shmDat = { .err = errno, .ret = ret };
      plr_copyToShm(&shmDat, sizeof(shmDat), 0);
      return 0;
    }
    // All processes call plr_masterAction() to synchronize at this point
    plr_masterAction(masterAct);
    
    if (!plr_isMasterProcess()) {
      // Slaves copy return values from shared memory
      openShmData_t shmDat;
      plr_copyFromShm(&shmDat, sizeof(shmDat), 0);
      
      // If error occurred in master, just return master's values
      if (shmDat.ret < 0) {
        ret = shmDat.ret;
        errno = shmDat.err;
      } else {
        // Call original libc function, removing O_EXCL flag if it is given
        if (flags & O_CREAT) {
          ret = _open(pathname, flags & ~O_EXCL, mode);
        } else {
          ret = _open(pathname, flags & ~O_EXCL);
        }
      }
    }
    
    plr_clearInsidePLR();
    return ret;
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

#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "plr.h"
#include "plrLog.h"
#include "libc_func.h"
#include "crc32_util.h"

typedef struct {
  int err;
  int ret;
} unlinkShmData_t;

int unlink(const char *pathname) {
  // Get libc syscall function pointer & offset in image
  libc_func(unlink, int, const char *);
  
  if (plr_checkInsidePLR()) {
    // If already inside PLR code, just call original syscall & return
    return _unlink(pathname);
  } else {
    plr_setInsidePLR();
    plrlog(LOG_SYSCALL, "[%d:unlink] Unlink file %s\n", getpid(), pathname);
    
    syscallArgs_t args = {
      .addr = _off_unlink,
      .arg[0] = crc32(0, pathname, strlen(pathname)),
    };
    plr_checkSyscallArgs(&args);
    
    // Nested function actually performed by master process only
    int ret;
    int masterAct() {
      // Call original libc function
      ret = _unlink(pathname);
      
      // Store return value in shared memory for slave processes
      unlinkShmData_t shmDat = { .err = errno, .ret = ret };
      plr_copyToShm(&shmDat, sizeof(shmDat), 0);
      return 0;
    }
    // All processes call plr_masterAction() to synchronize at this point
    plr_masterAction(masterAct);
    
    if (!plr_isMasterProcess()) {
      // Slaves copy return values from shared memory
      unlinkShmData_t shmDat;
      plr_copyFromShm(&shmDat, sizeof(shmDat), 0);
      
      ret = shmDat.ret;
      errno = shmDat.err;
    }
    
    plr_clearInsidePLR();
    return ret;
  }
}

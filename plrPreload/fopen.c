#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "plr.h"
#include "plrLog.h"
#include "libc_func.h"
#include "crc32_util.h"

typedef struct {
  int err;
  int failed;
} fopenShmData_t;

FILE *fopen(const char *path, const char *mode) {
  // Get libc syscall function pointer & offset in image
  libc_func(fopen, FILE *, const char *, const char *);
  
  // If already inside PLR code, just call original syscall & return
  if (plr_checkInsidePLR()) {
    // Call original libc function
    return _fopen(path, mode);
  } else {
    plr_setInsidePLR();
    plrlog(LOG_SYSCALL, "[%d:fopen] Open file '%s' mode '%s'\n", getpid(), path, mode);
    
    syscallArgs_t args = {
      .addr = _off_fopen,
      .arg[0] = crc32(0, path, strlen(path)),
      .arg[1] = crc32(0, mode, strlen(mode)),
    };
    plr_checkSyscallArgs(&args);
    
    // Nested function actually performed by master process only
    FILE *ret;
    int masterAct() {
      // Hacky workaround...this makes sure the fd for the extraShm area
      // is open before calling open on other files, to prevent master & slave
      // fd's getting out of sync
      plr_copyToShm(NULL, 0, 0);
      
      // Call original libc function
      ret = _fopen(path, mode);
      
      // Store return value in shared memory for slave processes
      fopenShmData_t shmDat = { .err = errno, .failed = (ret == NULL) };
      plr_copyToShm(&shmDat, sizeof(shmDat), 0);
      return 0;
    }
    // All processes call plr_masterAction() to synchronize at this point
    plr_masterAction(masterAct);
    
    if (!plr_isMasterProcess()) {
      // Slaves copy return values from shared memory
      fopenShmData_t shmDat;
      plr_copyFromShm(&shmDat, sizeof(shmDat), 0);
      
      // If error occurred in master, just return master's values
      if (shmDat.failed) {
        errno = shmDat.err;
      } else {
        // Call original libc function
        ret = _fopen(path, mode);
      }
    }
    
    plr_clearInsidePLR();
    return ret;
  }
}

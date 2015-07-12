#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include "plr.h"
#include "libc_func.h"
#include "crc32c_util.h"

#include <stdio.h>

ssize_t write(int fd, const void *buf, size_t count) {  
  // Get libc syscall function pointer & offset in image
  libc_func(write, ssize_t, int, const void *, size_t);
    
  ssize_t ret;
  if (!plr_checkInsidePLR()) {
    plr_setInsidePLR();
    printf("[%d:write] Write %ld bytes to fd %d\n", getpid(), count, fd);
    
    // Not comparing buf argument, different processes could have different
    // VM mappings and still be valid
    syscallArgs_t args = {
      .addr = _off_write,
      .arg[0] = fd,
      .arg[1] = crc32(0, buf, count),
      .arg[2] = count
    };
    plr_checkSyscallArgs(&args);
    
    // Nested function actually performed by master process only
    int masterAct() {
      // Call original libc function
      ret = _write(fd, buf, count);
      // Store return value & returned data in shared memory for slave processes
      plr_copyToShm(&ret, sizeof(ssize_t), 0);
      plr_copyToShm(&errno, sizeof(errno), sizeof(ssize_t));
      return 0;
    }
    // All processes call plr_masterAction() to synchronize at this point
    plr_masterAction(masterAct);
    
    if (!plr_isMasterProcess()) {
      // Slaves copy return values from shared memory
      int errnoSave;
      plr_copyFromShm(&ret, sizeof(ssize_t), 0);
      plr_copyFromShm(&errnoSave, sizeof(errno), sizeof(ssize_t));
      
      // Slaves seek to new fd offset
      if (ret != -1) {
        lseek(fd, ret, SEEK_CUR);
      }
      errno = errnoSave;
    }
    
    plr_clearInsidePLR();
  } else {
    // If already inside PLR code, just call original syscall & return
    ret = _write(fd, buf, count);
  }
  
  return ret;
}
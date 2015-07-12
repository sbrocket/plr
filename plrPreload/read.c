#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include "plr.h"
#include "libc_func.h"

#include <stdio.h>

ssize_t read(int fd, void *buf, size_t count) {  
  // Get libc syscall function pointer & offset in image
  libc_func(read, ssize_t, int, void *, size_t);
    
  ssize_t ret;
  if (!plr_checkInsidePLR()) {
    plr_setInsidePLR();
    printf("[%d:read] Read (up to) %ld bytes from fd %d\n", getpid(), count, fd);
    
    // Not comparing buf argument, different processes could have different
    // VM mappings and still be valid
    syscallArgs_t args = {
      .addr = _off_read,
      .arg[0] = fd,
      .arg[1] = 0, //(unsigned long)buf,
      .arg[2] = count
    };
    plr_checkSyscallArgs(&args);
    
    // Nested function actually performed by master process only
    int masterAct() {
      // Call original libc function
      ret = _read(fd, buf, count);
      // Store return value & returned data in shared memory for slave processes
      plr_copyToShm(&ret, sizeof(ssize_t), 0);
      plr_copyToShm(&errno, sizeof(errno), sizeof(ssize_t));
      plr_copyToShm(buf, ret, sizeof(ssize_t)+sizeof(errno));
      return 0;
    }
    // All processes call plr_masterAction() to synchronize at this point
    plr_masterAction(masterAct);
    
    if (!plr_isMasterProcess()) {
      // Slaves copy return values from shared memory
      int errnoSave;
      plr_copyFromShm(&ret, sizeof(ssize_t), 0);
      plr_copyFromShm(&errnoSave, sizeof(errno), sizeof(ssize_t));
      plr_copyFromShm(buf, ret, sizeof(ssize_t)+sizeof(errno));
      
      // Slaves seek to new fd offset
      if (ret != -1) {
        lseek(fd, ret, SEEK_CUR);
      }
      errno = errnoSave;
    }
    
    plr_clearInsidePLR();
  } else {
    // If already inside PLR code, just call original syscall & return
    ret = _read(fd, buf, count);
  }
  
  return ret;
}
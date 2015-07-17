#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include "plr.h"
#include "libc_func.h"

#include <stdio.h>

typedef struct {
  int err;
  ssize_t ret;
  off_t offs;
} readShmData_t;

ssize_t read(int fd, void *buf, size_t count) {  
  // Get libc syscall function pointer & offset in image
  libc_func(read, ssize_t, int, void *, size_t);
    
  if (plr_checkInsidePLR()) {
    // If already inside PLR code, just call original syscall & return
    return _read(fd, buf, count);
  } else {
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
    ssize_t ret;
    int masterAct() {
      // Call original libc function
      ret = _read(fd, buf, count);
      
      // Use lseek to get new file offset
      readShmData_t shmDat = { .err = errno, .ret = ret };
      if (ret != -1) {
        shmDat.offs = lseek(fd, 0, SEEK_CUR);
      }
      
      // Store return value & returned data in shared memory for slave processes
      plr_copyToShm(&shmDat, sizeof(shmDat), 0);
      plr_copyToShm(buf, ret, sizeof(shmDat));
      return 0;
    }
    // All processes call plr_masterAction() to synchronize at this point
    plr_masterAction(masterAct);
    
    if (!plr_isMasterProcess()) {
      // Slaves copy return values from shared memory
      readShmData_t shmDat;
      plr_copyFromShm(&shmDat, sizeof(shmDat), 0);
      plr_copyFromShm(buf, shmDat.ret, sizeof(shmDat));
      
      // Slaves seek to new fd offset
      // Can't use SEEK_CUR and advance by ret because the slave processes
      // may have been forked from each other after the fd was opened, in which
      // case the fd & its offset are shared, and that would advance more than needed
      if (shmDat.ret != -1) {
        lseek(fd, shmDat.offs, SEEK_SET);
      }
      
      // Return same value & errno as master
      ret = shmDat.ret;
      errno = shmDat.err;
    }
    
    plr_clearInsidePLR();
    return ret;
  }
}
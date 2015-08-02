#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include "plr.h"
#include "plrLog.h"
#include "plrSharedData.h"
#include "libc_func.h"

typedef struct {
  int err;
  int ret;
  long offs;
  int ferr;
} fseekShmData_t;

int fseek(FILE *stream, long offset, int whence) {
  // Get libc syscall function pointer & offset in image
  libc_func(fseek, int, FILE *, long, int);
  
  if (plr_checkInsidePLR()) {
    // If already inside PLR code, just call original syscall & return
    return _fseek(stream, offset, whence);
  } else {
    plr_setInsidePLR();
    int fn = fileno(stream);
    int w = whence;
    const char *wStr = ((w == SEEK_CUR) ? "SEEK_CUR" : ((w == SEEK_SET) ? "SEEK_SET" : ((w == SEEK_END) ? "SEEK_END" : "INVALID")));
    plrlog(LOG_SYSCALL, "[%d:fseek] Fseek to %s %ld on fileno %d\n", getpid(), wStr, offset, fn);
    
    syscallArgs_t args = {
      .addr = _off_fseek,
      .arg[0] = fn,
      .arg[1] = offset,
      .arg[2] = whence
    };
    plr_checkSyscallArgs(&args);
    
    int ret;
    int masterAct() {
      // Call original libc function
      plrlog(LOG_DEBUG, "[%d:fseek] M: File pos before fseek (%s %ld) = %ld\n", getpid(), wStr, offset, ftell(stream));
      ret = _fseek(stream, offset, whence);
      plrlog(LOG_DEBUG, "[%d:fseek] M: File pos after fseek (%s %ld) = %ld\n", getpid(), wStr, offset, ftell(stream));
      
      // Use ftell to get new file offset
      fseekShmData_t shmDat = { .err = errno, .ret = ret };
      shmDat.offs = ftell(stream);
      shmDat.ferr = ferror(stream);
      
      // Store return value & returned data in shared memory for slave processes
      plr_copyToShm(&shmDat, sizeof(shmDat), 0);
      
      if (shmDat.ferr) {
        // Not sure how to handle passing ferror's to slaves yet, no way 
        // to manually set error state
        plrlog(LOG_ERROR, "[%d:fseek] ferror (%d) occurred (%d)\n", getpid(), shmDat.ferr, fn);
        exit(1);
      }
      
      return 0;
    }
    // All processes call plr_masterAction() to synchronize at this point
    plr_masterAction(masterAct);
    
    fseekShmData_t shmDat;
    if (!plr_isMasterProcess()) {
      // Slaves copy return values from shared memory
      plr_copyFromShm(&shmDat, sizeof(shmDat), 0);
      
      // Slaves seek to new fd offset from parent
      _fseek(stream, shmDat.offs, SEEK_SET);
      
      if (shmDat.ferr) {
        plrlog(LOG_ERROR, "[%d:fseek] ferror (%d) from master (%d)\n", getpid(), shmDat.ferr, fn);
        exit(1);
      }
      
    }
    
    // TEMPORARY
    // Slave processes sometimes end up with the wrong file offset, even after SEEK_SET
    // Compare file state at exit to make sure everything is consistent
    // Piggybacking off checkSyscallArgs mechanism to do this
    syscallArgs_t exitState = {
      .arg[0] = ftell(stream),
      .arg[1] = feof(stream),
      .arg[2] = ferror(stream),
    };
    plr_checkSyscallArgs(&exitState);
    
    if (!plr_isMasterProcess()) {  
      // Return same value & errno as master
      ret = shmDat.ret;
      errno = shmDat.err;
    }
    
    plr_clearInsidePLR();
    return ret;
  }
}

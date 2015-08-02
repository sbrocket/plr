#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include "plr.h"
#include "plrLog.h"
#include "libc_func.h"
#include "crc32_util.h"

typedef struct {
  int err;
  int ret;
  long offs;
  int eof;
  int ferr;
} fgetcShmData_t;

int com_fgetc(const char *fncName, FILE *stream) {
  // Get libc syscall function pointer & offset in image
  libc_func(fgetc, int, FILE *);
    
  if (plr_checkInsidePLR()) {
    // If already inside PLR code, just call original syscall & return
    return _fgetc(stream);
  } else {
    plr_setInsidePLR();
    int fn = fileno(stream);
    plrlog(LOG_SYSCALL, "[%d:%s] Read char from fileno %d\n", getpid(), fncName, fn);
    
    syscallArgs_t args = {
      .addr = _off_fgetc,
      .arg[0] = fn
    };
    plr_checkSyscallArgs(&args);
    
    // Nested function actually performed by master process only
    int ret;
    int masterAct() {
      // Call original libc function
      ret = _fgetc(stream);
      
      // Use ftell to get new file offset
      fgetcShmData_t shmDat = { .err = errno, .ret = ret };
      shmDat.offs = ftell(stream);
      shmDat.eof = feof(stream);
      shmDat.ferr = ferror(stream);
      
      // Store return value & returned data in shared memory for slave processes
      plr_copyToShm(&shmDat, sizeof(shmDat), 0);
      
      if (shmDat.ferr) {
        // Not sure how to handle passing ferror's to slaves yet, no way 
        // to manually set error state
        plrlog(LOG_ERROR, "[%d:%s] ferror (%d) occurred (%d)\n", getpid(), fncName, shmDat.ferr, fn);
        exit(1);
      }
      
      return 0;
    }
    // All processes call plr_masterAction() to synchronize at this point
    plr_masterAction(masterAct);
    
    if (!plr_isMasterProcess()) {
      // Slaves copy return values from shared memory
      fgetcShmData_t shmDat;
      plr_copyFromShm(&shmDat, sizeof(shmDat), 0);
      
      // Slaves seek to new fd offset
      // Can't use SEEK_CUR and advance by ret because the slave processes
      // may have been forked from each other after the fd was opened, in which
      // case the fd & its offset are shared, and that would advance more than needed
      fseek(stream, shmDat.offs, SEEK_SET);
      if (shmDat.eof) {
        // fgetc at EOF to set feof indicator
        if (_fgetc(stream) != -1) {
          plrlog(LOG_ERROR, "[%d:%s] fgetc to cause feof actually read data\n", getpid(), fncName);
          exit(1);
        }
      }
      if (shmDat.ferr) {
        plrlog(LOG_ERROR, "[%d:%s] ferror (%d) from master (%d)\n", getpid(), fncName, shmDat.ferr, fn);
        exit(1);
      }
      
      // Return same value & errno as master
      ret = shmDat.ret;
      errno = shmDat.err;
    }
    
    plr_clearInsidePLR();
    return ret;
  }
}

int fgetc(FILE *stream) {
  return com_fgetc("fgetc",stream);
}

int getc(FILE *stream) {
  return com_fgetc("getc",stream);
}

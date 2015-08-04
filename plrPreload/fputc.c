#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include "plr.h"
#include "plrLog.h"
#include "plrSharedData.h"
#include "libc_func.h"
#include "crc32_util.h"

typedef struct {
  int err;
  int ret;
  long offs;
  int eof;
  int ferr;
} fputcShmData_t;

int com_fputc(const char *fncName, int c, FILE *stream) {
  // Get libc syscall function pointer & offset in image
  libc_func(fputc, int, int, FILE *);
    
  if (plr_checkInsidePLR()) {
    // If already inside PLR code, just call original syscall & return
    return _fputc(c, stream);
  } else {
    plr_setInsidePLR();
    int fn = fileno(stream);
    plrlog(LOG_SYSCALL, "[%d:%s] Write '%c' to fileno %d\n", getpid(), fncName, c, fn);
    
    syscallArgs_t args = {
      .addr = _off_fputc,
      .arg[0] = crc32(0, fncName, strlen(fncName)),
      .arg[1] = fn,
      .arg[2] = c,
    };
    plr_checkSyscallArgs(&args);
    
    // Nested function actually performed by master process only
    int ret;
    fputcShmData_t shmDat;
    int masterAct() {
      // Call original libc function
      ret = _fputc(c, stream);
      
      // Flush/sync data to disk to help other processes see it
      fflush(stream);
      fsync(fn);
      
      // Use ftell to get new file offset
      shmDat.err = errno;
      shmDat.ret = ret;
      shmDat.offs = ftell(stream);
      shmDat.eof = feof(stream);
      shmDat.ferr = ferror(stream);
      
      // Store return value in shared memory for slave processes
      plr_copyToShm(&shmDat, sizeof(shmDat), 0);
      
      if (shmDat.ferr) {
        // Not sure how to handle passing ferror's to slaves yet, no way 
        // to manually set error state
        plrlog(LOG_ERROR, "[%d:%s] ERROR: ferror (%d) occurred (%d)\n", getpid(), fncName, shmDat.ferr, fn);
        exit(1);
      }
      
      return 0;
    }
    // All processes call plr_masterAction() to synchronize at this point
    plr_masterAction(masterAct);
    
    if (!plr_isMasterProcess()) {
      // Slaves copy return values from shared memory
      plr_copyFromShm(&shmDat, sizeof(shmDat), 0);
      
      // Slaves seek to new fd offset
      // Can't use SEEK_CUR and advance by ret because the slave processes
      // may have been forked from each other after the fd was opened, in which
      // case the fd & its offset are shared, and that would advance more than needed
      fseek(stream, shmDat.offs, SEEK_SET);
      
      // Necessary to manually reset EOF flag because fseek clears it
      if (shmDat.eof) {
        // fgetc at EOF to set feof indicator
        int c;
        if ((c = fgetc(stream)) != EOF) {
          const char *fmt = "[%d:%s] ERROR: fgetc to cause EOF actually got data (%c %d), ftell = %d, feof = %d\n";
          plrlog(LOG_ERROR, fmt, getpid(), fncName, c, c, ftell(stream), feof(stream));
          exit(1);
        }
      }
      if (shmDat.ferr) {
        plrlog(LOG_ERROR, "[%d:%s] ERROR: ferror (%d) from master (%d)\n", getpid(), fncName, shmDat.ferr, fn);
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
    
    // All procs return same value & errno
    ret = shmDat.ret;
    errno = shmDat.err;
    
    plr_clearInsidePLR();
    return ret;
  }
}

int fputc(int c, FILE *stream) {
  return com_fputc("fputc", c, stream);
}

int putc(int c, FILE *stream) {
  return com_fputc("putc", c, stream);
}

int putchar(int c) {
  return com_fputc("getchar", c, stdout);
}

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
  size_t ret;
  long offs;
  int eof;
  int ferr;
} freadShmData_t;

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
  // Get libc syscall function pointer & offset in image
  libc_func(fread, size_t, void *, size_t, size_t, FILE *);
  
  if (plr_checkInsidePLR()) {
    // If already inside PLR code, just call original syscall & return
    return _fread(ptr, size, nmemb, stream);
  } else {
    plr_setInsidePLR();
    int fn = fileno(stream);
    plrlog(LOG_SYSCALL, "[%d:fread] Read %ld %ld-byte elems from fileno %d\n", getpid(), nmemb, size, fn);
    
    syscallArgs_t args = {
      .addr = _off_fread,
      .arg[0] = fn,
      .arg[1] = size,
      .arg[2] = nmemb,
    };
    plr_checkSyscallArgs(&args);
    
    // Nested function actually performed by master process only
    size_t ret;
    freadShmData_t shmDat;
    int masterAct() {
      // Call original libc function
      ret = _fread(ptr, size, nmemb, stream);
      
      // Use ftell to get new file offset
      shmDat.err = errno;
      shmDat.ret = ret;
      shmDat.offs = ftell(stream);
      shmDat.eof = feof(stream);
      shmDat.ferr = ferror(stream);
      
      // Store return value & returned data in shared memory for slave processes
      plr_copyToShm(&shmDat, sizeof(shmDat), 0);
      plr_copyToShm(ptr, ret, sizeof(shmDat));
      
      if (shmDat.ferr) {
        // Not sure how to handle passing ferror's to slaves yet, no way
        // to manually set error state
        plrlog(LOG_ERROR, "[%d:fread] ferror (%d) occurred (%d)\n", getpid(), shmDat.ferr, fn);
        exit(1);
      }
      
      return 0;
    }
    // All processes call plr_masterAction() to synchronize at this point
    plr_masterAction(masterAct);
    
    if (!plr_isMasterProcess()) {
      // Slaves copy return values from shared memory
      plr_copyFromShm(&shmDat, sizeof(shmDat), 0);
      if (shmDat.ret > 0) {
        plr_copyFromShm(ptr, shmDat.ret, sizeof(shmDat));
      }
      
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
          const char *fmt = "[%d:fread] ERROR: fgetc to cause EOF actually got data (%c %d), ftell = %d, feof = %d\n";
          plrlog(LOG_ERROR, fmt, getpid(), c, c, ftell(stream), feof(stream));
          exit(1);
        }
      }
      if (shmDat.ferr) {
        plrlog(LOG_ERROR, "[%d:fread] ERROR: ferror (%d) from master (%d)\n", getpid(), shmDat.ferr, fn);
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

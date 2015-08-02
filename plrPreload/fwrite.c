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
  ssize_t ret;
  long offs;
  int eof;
  int ferr;
} fwriteShmData_t;

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
  // Get libc syscall function pointer & offset in image
  libc_func(fwrite, size_t, const void *, size_t, size_t, FILE *);
    
  if (plr_checkInsidePLR()) {
    // If already inside PLR code, just call original syscall & return
    return _fwrite(ptr, size, nmemb, stream);
  } else {
    plr_setInsidePLR();
    int fn = fileno(stream);
    plrlog(LOG_SYSCALL, "[%d:fwrite] Write %ld %ld-byte elems to fileno %d\n", getpid(), nmemb, size, fn);
    plrlog(LOG_DEBUG, "[%d:fwrite] '%.*s'\n", getpid(), (int)(nmemb*size), (char*)ptr);
    
    syscallArgs_t args = {
      .addr = _off_fwrite,
      .arg[0] = fn,
      .arg[1] = size,
      .arg[2] = nmemb,
      .arg[3] = crc32(0, ptr, size*nmemb),
    };
    plr_checkSyscallArgs(&args);
    
    // Nested function actually performed by master process only
    size_t ret;
    int masterAct() {
      // Call original libc function
      ret = _fwrite(ptr, size, nmemb, stream);
      
      // Use ftell to get new file offset
      fwriteShmData_t shmDat = { .err = errno, .ret = ret };
      shmDat.offs = ftell(stream);
      shmDat.eof = feof(stream);
      shmDat.ferr = ferror(stream);
      
      // Store return value in shared memory for slave processes
      plr_copyToShm(&shmDat, sizeof(shmDat), 0);
      
      if (shmDat.ferr) {
        // Not sure how to handle passing ferror's to slaves yet, no way 
        // to manually set error state
        plrlog(LOG_ERROR, "[%d:fwrite] ferror (%d) occurred (%ld %ld %d)\n", getpid(), shmDat.ferr, nmemb, size, fn);
        exit(1);
      }
      
      return 0;
    }
    // All processes call plr_masterAction() to synchronize at this point
    plr_masterAction(masterAct);
    
    if (!plr_isMasterProcess()) {
      // Slaves copy return values from shared memory
      fwriteShmData_t shmDat;
      plr_copyFromShm(&shmDat, sizeof(shmDat), 0);
      
      // Slaves seek to new fd offset
      // Can't use SEEK_CUR and advance by ret because the slave processes
      // may have been forked from each other after the fd was opened, in which
      // case the fd & its offset are shared, and that would advance more than needed
      fseek(stream, shmDat.offs, SEEK_SET);
      if (shmDat.eof) {
        // fgetc at EOF to set feof indicator
        if (fgetc(stream) != -1) {
          plrlog(LOG_ERROR, "[%d:fwrite] fgetc to cause feof actually read data\n", getpid());
          exit(1);
        }
      }
      if (shmDat.ferr) {
        plrlog(LOG_ERROR, "[%d:fwrite] ferror (%d) from master (%ld %ld %d)\n", getpid(), shmDat.ferr, nmemb, size, fn);
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

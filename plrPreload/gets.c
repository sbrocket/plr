#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "plr.h"
#include "plrLog.h"
#include "libc_func.h"
#include "crc32_util.h"

typedef struct {
  int err;
  int retNull;
  size_t sLen;
  long offs;
  int eof;
  int ferr;
} getsShmData_t;

char *gets(char *s) {
  // Get libc syscall function pointer & offset in image
  libc_func(gets, char *, char *);
    
  if (plr_checkInsidePLR()) {
    // If already inside PLR code, just call original syscall & return
    return _gets(s);
  } else {
    plr_setInsidePLR();
    plrlog(LOG_SYSCALL, "[%d:gets] Read line from stdin\n", getpid());
    
    syscallArgs_t args = {
      .addr = _off_gets,
    };
    plr_checkSyscallArgs(&args);
    
    // Nested function actually performed by master process only
    char *ret;
    getsShmData_t shmDat;
    int masterAct() {
      // Call original libc function
      ret = _gets(s);
      
      // Use ftell to get new file offset
      shmDat.err = errno;
      shmDat.retNull = (ret == NULL);
      shmDat.sLen = (ret == NULL) ? 0 : strlen(s)+1;
      shmDat.offs = ftell(stdin);
      shmDat.eof = feof(stdin);
      shmDat.ferr = ferror(stdin);
      
      // Store return value & returned data in shared memory for slave processes
      plr_copyToShm(&shmDat, sizeof(shmDat), 0);
      if (!shmDat.retNull) {
        plr_copyToShm(s, shmDat.sLen, sizeof(shmDat));
      }
      
      if (shmDat.ferr) {
        // Not sure how to handle passing ferror's to slaves yet, no way 
        // to manually set error state
        plrlog(LOG_ERROR, "[%d:gets] ERROR: ferror (%d) occurred\n", getpid(), shmDat.ferr);
        exit(1);
      }
      
      return 0;
    }
    // All processes call plr_masterAction() to synchronize at this point
    plr_masterAction(masterAct);
    
    if (!plr_isMasterProcess()) {
      // Slaves copy return values from shared memory
      plr_copyFromShm(&shmDat, sizeof(shmDat), 0);
      if (!shmDat.retNull) {
        plr_copyFromShm(s, shmDat.sLen, sizeof(shmDat));
      }
      
      // Slaves seek to new fd offset
      // Can't use SEEK_CUR and advance by ret because the slave processes
      // may have been forked from each other after the fd was opened, in which
      // case the fd & its offset are shared, and that would advance more than needed
      fseek(stdin, shmDat.offs, SEEK_SET);
      
      // Necessary to manually reset EOF flag because fseek clears it
      if (shmDat.eof) {
        // fgetc at EOF to set feof indicator
        int c;
        if ((c = fgetc(stdin)) != EOF) {
          const char *fmt = "[%d:gets] ERROR: fgetc to cause EOF actually got data (%c %d), ftell = %d, feof = %d\n";
          plrlog(LOG_ERROR, fmt, getpid(), c, c, ftell(stdin), feof(stdin));
          exit(1);
        }
      }
      if (shmDat.ferr) {
        plrlog(LOG_ERROR, "[%d:gets] ERROR: ferror (%d) from master\n", getpid(), shmDat.ferr);
        exit(1);
      }
    }
    
    // TEMPORARY
    // Slave processes sometimes end up with the wrong file offset, even after SEEK_SET
    // Compare file state at exit to make sure everything is consistent
    // Piggybacking off checkSyscallArgs mechanism to do this
    syscallArgs_t exitState = {
      .arg[0] = ftell(stdin),
      .arg[1] = feof(stdin),
      .arg[2] = ferror(stdin),
    };
    plr_checkSyscallArgs(&exitState);
    
    // All procs return same value & errno
    ret = (shmDat.retNull) ? NULL : s;
    errno = shmDat.err;
    
    plr_clearInsidePLR();
    return ret;
  }
}

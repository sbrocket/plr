// _GNU_SOURCE for asprintf/vasprintf
#define _GNU_SOURCE
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include "plr.h"
#include "plrLog.h"
#include "stringUtil.h"
#include "libc_func.h"
#include "crc32_util.h"

typedef struct {
  int err;
  int ret;
} printfShmData_t;

int com_vfprintf_chk(const char *fncName, FILE *stream, int flag, const char *format, va_list ap) {
  // Get libc syscall function pointer & offset in image
  libc_func(__vfprintf_chk, int, FILE *, int, const char *, va_list);
    
  if (plr_checkInsidePLR()) {
    // If already inside PLR code, just call original syscall & return
    return ___vfprintf_chk(stream, flag, format, ap);
  } else {
    plr_setInsidePLR();
    int fn = fileno(stream);
    if (plrlogIsEnabled(LOG_SYSCALL)) {
      char *formatFmt = str_expandEscapes(format);
      plrlog(LOG_SYSCALL, "[%d:%s] Fileno %d, format '%s'\n", getpid(), fncName, fn, formatFmt);
      free(formatFmt);
    }
    
    // Use vasprintf to compute final string so that arguments can be checked 
    // in addition to format
    va_list apCopy;
    va_copy(apCopy, ap);
    char *resStr;
    int vasRet = vasprintf(&resStr, format, apCopy);
    va_end(apCopy);
    if (plrlogIsEnabled(LOG_DEBUG)) {
      char *resStrFmt = str_expandEscapes(resStr);
      plrlog(LOG_DEBUG, "[%d:%s] Str = '%s'\n", getpid(), fncName, resStrFmt);
      free(resStrFmt);
    }
    
    syscallArgs_t args = {
      .addr = _off___vfprintf_chk,
      .arg[0] = crc32(0, fncName, strlen(fncName)),
      .arg[1] = fn,
      .arg[2] = ((vasRet) ? crc32(0, resStr, strlen(resStr)) : 0),
      .arg[3] = flag,
    };
    plr_checkSyscallArgs(&args);
    if (vasRet != -1) {
      free(resStr);
    }
    
    // Nested function actually performed by master process only
    ssize_t ret;
    int masterAct() {
      // Call original libc function
      ret = ___vfprintf_chk(stream, flag, format, ap);
      
      // Store return value in shared memory for slave processes
      printfShmData_t shmDat = { .err = errno, .ret = ret };
      plr_copyToShm(&shmDat, sizeof(shmDat), 0);
      return 0;
    }
    // All processes call plr_masterAction() to synchronize at this point
    plr_masterAction(masterAct);
    
    if (!plr_isMasterProcess()) {
      // Slaves copy return values from shared memory
      printfShmData_t shmDat;
      plr_copyFromShm(&shmDat, sizeof(shmDat), 0);
      
      // Return same value & errno as master
      ret = shmDat.ret;
      errno = shmDat.err;
    }
    
    plr_clearInsidePLR();
    return ret;
  }
}

int __vfprintf_chk(FILE *stream, int flag, const char *format, va_list ap) {  
  return com_vfprintf_chk("vfprintf", stream, flag, format, ap);
}

int __vprintf_chk(int flag, const char *format, va_list ap) {
  return com_vfprintf_chk("vprintf", stdout, flag, format, ap);
}

int __fprintf_chk(FILE *stream, int flag, const char *format, ...) {
  va_list argp;
  va_start(argp, format);
  int ret = com_vfprintf_chk("fprintf", stream, flag, format, argp);
  va_end(argp);
  return ret;
}

int __printf_chk(int flag, const char *format, ...) {
  va_list argp;
  va_start(argp, format);
  int ret = com_vfprintf_chk("printf", stdout, flag, format, argp);
  va_end(argp);
  return ret;
}

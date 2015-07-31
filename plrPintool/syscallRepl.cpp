#include <iostream>
#include "syscallRepl.h"
#include "pin.H"
#include <assert.h>

#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include "plrSharedData.h"
#include "plr.h"

//ssize_t emul_read(const CONTEXT *ctxt, AFUNPTR origFnc, int fd, void *buf, size_t count) {
//  //printf("[%d] emul_read %ld bytes from fd %d\n", getpid(), count, fd);
//  
//  ssize_t res;
//  CALL_ORIG(origFnc, ctxt, res, fd, buf, count)
//  return res;
//}

ssize_t emul_write(const CONTEXT *ctxt, AFUNPTR origFnc, int fd, const void *buf, size_t count) {
  plr_refreshSharedData();
  printf("[%d] emul_write: &plrShm = %p, plrShm = %p, myProcShm = %p\n", getpid(), &plrShm, plrShm, myProcShm);
  
  //printf("[%d] emul_write %ld bytes to fd %d\n", getpid(), count, fd);
  
  ssize_t res;
  CALL_APP_FUN(origFnc, ctxt, res, fd, buf, count)
  return res;
  //return orig_write(fd, buf, count);
}

//REPLACE_SYSCALL(read, emul_read, ssize_t, int, void*, size_t)
//REPLACE_SYSCALL(write, emul_write, ssize_t, int, const void*, size_t)

replFunc_t replFuncs[20];
int replFuncCount = 0;
int imageLoad_libcFound = 0;
void syscallRepl_registerReplFunc(replFunc_t func) {
  assert(replFuncCount < 20);
  assert(!imageLoad_libcFound);
  
  replFuncs[replFuncCount] = func;
  replFuncCount++;
}

void syscallRepl_imageLoad(IMG img, void *v) {  
  string imgName = IMG_Name(img);
  string targLib = "libc.so.6";
  if (imgName.length() >= targLib.length() 
      && imgName.substr(imgName.length()-targLib.length()) == targLib)
  {
    imageLoad_libcFound = 1;
    for (int i = 0; i < replFuncCount; ++i) {
      replFunc_t func = replFuncs[i];
      func(img);
    }
  }
}

void syscallRepl_replInNewProcess() {
  PIN_LockClient();
  for (IMG img = APP_ImgHead(); IMG_Valid(img); img = IMG_Next(img)) {
    syscallRepl_imageLoad(img, NULL);
  }
  PIN_UnlockClient();
}

void syscallRepl_main() {
  IMG_AddInstrumentFunction(syscallRepl_imageLoad, NULL);
}

#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include "libc_func.h"

void *get_libc_func(const char *funcName, void **offset) {
  void *funcPtr = dlsym(RTLD_NEXT, funcName);
  assert(funcPtr);
  
  // Get the offset of the function from the load address
  Dl_info funcInfo;
  int ret = dladdr(funcPtr, &funcInfo);
  assert(ret != 0);
  *offset = (void*)(funcPtr - funcInfo.dli_fbase);
  
  return funcPtr;
}

#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include "plr.h"
#include "syscallRepl.h"

#include <stdio.h>

// PIN_ClientFork is missing from Pin headers, adding definition here
namespace LEVEL_PINCLIENT {
  extern OS_THREAD_ID PIN_ClientFork();
}

pid_t emul_fork(const CONTEXT *ctxt, AFUNPTR origFnc) {
  plr_refreshSharedData();

  if (plr_checkInsidePLR()) {
    // If already inside PLR code, just call original syscall & return
    printf("[%d:fork_int]\n", PIN_GetPid());
    return PIN_ClientFork();
  } else {
    printf("[%d] PLR does not support programs that call fork()\n", PIN_GetPid());
    PIN_ExitApplication(1);
    return 0;
  }
}

//REPLACE_SYSCALL(fork, emul_fork, pid_t)

void doRepl_fork (IMG img) {
  RTN rtn = RTN_FindByName(img, "fork");
  if (RTN_Valid(rtn)) {
    PROTO proto = PROTO_Allocate(PIN_PARG(pid_t), CALLINGSTD_DEFAULT,
                                 "fork",
                                 PIN_PARG_END());
    RTN_ReplaceSignature(rtn, (AFUNPTR)emul_fork,
                         IARG_PROTOTYPE, proto,
                         IARG_CONST_CONTEXT,
                         IARG_ORIG_FUNCPTR,
                         IARG_END);
    PROTO_Free(proto);
  } else {
    fprintf(stderr, "[%d] Didn't find '%s' in libc", PIN_GetPid(), "fork");
    PIN_ExitApplication(2);
  }
}

__attribute__((constructor))
static void initRepl_fork() {
  syscallRepl_registerReplFunc(doRepl_fork);
}

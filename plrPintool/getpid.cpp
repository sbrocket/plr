#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include "plr.h"
#include "syscallRepl.h"

#include <stdio.h>

#include "plrSharedData.h"

//static const CONTEXT *m_ctxt;
//static AFUNPTR m_origFnc;
//static pid_t m_ret;
//static int masterAct() {
//  // Call original libc function
//  // TODO: Need 0 argument support in macros
//  // CALL_APP_FUN(m_origFnc, m_ctxt, m_ret)
//  PIN_CallApplicationFunction(m_ctxt, PIN_ThreadId(),
//                              CALLINGSTD_DEFAULT,
//                              (AFUNPTR)m_origFnc, NULL,
//                              PIN_PARG(pid_t), &m_ret,
//                              PIN_PARG_END());
//
//  // Store return value & returned data in shared memory for slave processes
//  //plr_copyToShm(&shmDat, sizeof(shmDat), 0);
//  //plr_copyToShm(m_buf, m_ret, sizeof(shmDat));
//  return 0;
//}

// TODO: When plr_processInit is called for each child process, after ApplicationStart,
// it calls plr_refreshSharedData which calls getpid, but plr_checkInsidePLR returns 0
// because it looks at the wrong g_insidePLRInternal

pid_t emul_getpid(const CONTEXT *ctxt, AFUNPTR origFnc) {
  //printf("getpid\n");
  plr_refreshSharedData();

  if (plr_checkInsidePLR()) {
    // If already inside PLR code, just call original syscall & return
    printf("[%d:getpid_int]\n", PIN_GetPid());
    return PIN_GetPid();
  } else {
    plr_setInsidePLR();
    printf("[%d:getpid]\n", PIN_GetPid());

    syscallArgs_t args = {};
    args.addr = (void*)0x5678; // TODO: Replace _off_getpid
    plr_checkSyscallArgs(&args);

    //// All processes call plr_masterAction() to synchronize at this point,
    //// but function actually only performed by master process
    //m_ctxt = ctxt;
    //m_origFnc = origFnc;
    //plr_masterAction(masterAct);
    //
    //pid_t ret;
    //if (plr_isMasterProcess()) {
    //  ret = m_ret;
    //} else {
    //  // Slaves copy return values from shared memory
    //  readShmData_t shmDat;
    //  plr_copyFromShm(&shmDat, sizeof(shmDat), 0);
    //  plr_copyFromShm(buf, shmDat.ret, sizeof(shmDat));
    //
    //  // Slaves seek to new fd offset
    //  // Can't use SEEK_CUR and advance by ret because the slave processes
    //  // may have been forked from each other after the fd was opened, in which
    //  // case the fd & its offset are shared, and that would advance more than needed
    //  if (shmDat.ret != -1) {
    //    lseek(fd, shmDat.offs, SEEK_SET);
    //  }
    //
    //  // Return same value & errno as master
    //  ret = shmDat.ret;
    //  errno = shmDat.err;
    //}

    pid_t ret;
    PIN_CallApplicationFunction(ctxt, PIN_ThreadId(),
                              CALLINGSTD_DEFAULT,
                              (AFUNPTR)origFnc, NULL,
                              PIN_PARG(pid_t), &ret,
                              PIN_PARG_END());

    plr_clearInsidePLR();
    return ret;
  }
}

//REPLACE_SYSCALL(getpid, emul_getpid, pid_t)

//pid_t repl_getpid(const CONTEXT *ctxt, AFUNPTR origFnc) {
//  pid_t ret;
//  CALL_APP_FUN(emul_getpid, ctxt, ret, ctxt, origFnc)
//  return ret;
//}

void doRepl_getpid (IMG img) {
  RTN rtn = RTN_FindByName(img, "getpid");
  if (RTN_Valid(rtn)) {
    PROTO proto = PROTO_Allocate(PIN_PARG(pid_t), CALLINGSTD_DEFAULT,
                                 "getpid",
                                 PIN_PARG_END());
    RTN_ReplaceSignature(rtn, (AFUNPTR)emul_getpid,
                         IARG_PROTOTYPE, proto,
                         IARG_CONST_CONTEXT,
                         IARG_ORIG_FUNCPTR,
                         IARG_END);
    PROTO_Free(proto);
  } else {
    fprintf(stderr, "[%d] Didn't find '%s' in libc", PIN_GetPid(), "getpid");
    PIN_ExitApplication(2);
  }
}

__attribute__((constructor))
static void initRepl_getpid() {
  syscallRepl_registerReplFunc(doRepl_getpid);
}

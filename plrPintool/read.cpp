#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include "plr.h"
#include "syscallRepl.h"

#include <stdio.h>

typedef struct {
  int err;
  ssize_t ret;
  off_t offs;
} readShmData_t;

//static const CONTEXT *m_ctxt;
//static AFUNPTR m_origFnc;
//static int m_fd;
//static void *m_buf;
//static size_t m_count;
//static ssize_t m_ret;
//static int masterAct() {
//  // Call original libc function
//  CALL_ORIG(m_origFnc, m_ctxt, m_ret, m_fd, m_buf, m_count)
//  
//  // Use lseek to get new file offset
//  //readShmData_t shmDat = { .err = errno, .ret = ret };
//  readShmData_t shmDat;
//  shmDat.err = errno;
//  shmDat.ret = m_ret;
//  if (m_ret != -1) {
//    shmDat.offs = lseek(m_fd, 0, SEEK_CUR);
//  }
//  
//  // Store return value & returned data in shared memory for slave processes
//  plr_copyToShm(&shmDat, sizeof(shmDat), 0);
//  plr_copyToShm(m_buf, m_ret, sizeof(shmDat));
//  return 0;
//}

ssize_t emul_read(const CONTEXT *ctxt, AFUNPTR origFnc, int fd, void *buf, size_t count) {
  plr_refreshSharedData();
  
  if (plr_checkInsidePLR()) {
    // If already inside PLR code, just call original syscall & return
    //ssize_t ret = origFnc(fd, buf, count);
    ssize_t ret;
    CALL_APP_FUN(origFnc, ctxt, ret, fd, buf, count)
    return ret;
  } else {
    plr_setInsidePLR();
    int myPid = getpid();
    printf("[%d:read] Read (up to) %ld bytes from fd %d\n", myPid, count, fd);
    
    // Not comparing buf argument, different processes could have different
    // VM mappings and still be valid
    syscallArgs_t args = {};
    args.addr = (void*)0x1234; // TODO: Replace _off_read
    args.arg[0] = fd;
    args.arg[1] = 0; //(unsigned long)buf;
    args.arg[2] = count;
    plr_checkSyscallArgs(&args);
    
    // Check to see if pid changed, indicates replacement process created
    int curPid = getpid();
    if (myPid != curPid) {
      myPid = curPid;
      printf("[%d] New process found, injecting replaced syscalls\n", myPid);
      syscallRepl_replInNewProcess();
    }
    
    printf("[%d] After read\n", myPid);
    
    //// All processes call plr_masterAction() to synchronize at this point,
    //// but function actually only performed by master process
    //m_ctxt = ctxt;
    //m_origFnc = origFnc;
    //m_fd = fd;
    //m_buf = buf;
    //m_count = count;
    //plr_masterAction(masterAct);
    //
    //ssize_t ret;
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
    
    ssize_t ret;
    CALL_APP_FUN(origFnc, ctxt, ret, fd, buf, count)
    
    plr_clearInsidePLR();
    return ret;
  }
}

//ssize_t repl_read(const CONTEXT *ctxt, AFUNPTR origFnc, int fd, void *buf, size_t count) {
//  ssize_t ret;
//  CALL_APP_FUN(emul_read, ctxt, ret, ctxt, origFnc, fd, buf, count)
//  return ret;
//}

REPLACE_SYSCALL(read, emul_read, ssize_t, int, void*, size_t)

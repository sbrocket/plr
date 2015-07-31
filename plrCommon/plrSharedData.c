// _GNU_SOURCE needed for mremap
#define _GNU_SOURCE
#include <unistd.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include "plrLog.h"
#include "plrSharedData.h"
#include "pthreadUtil.h"

///////////////////////////////////////////////////////////////////////////////
// Global data
plrData_t *plrShm = NULL;
perProcData_t *allProcShm = NULL;
perProcData_t *myProcShm = NULL;
char *extraShm = NULL;

///////////////////////////////////////////////////////////////////////////////
// Private functions
static int plrSD_getShmName(char name[NAME_MAX]);
static int plrSD_extraShmOffset();
static int plrSD_openShmFile(int oflag, mode_t mode);

///////////////////////////////////////////////////////////////////////////////

int plrSD_initSharedData(int nProc) {
  int shmFd = plrSD_openShmFile(O_CREAT | O_EXCL | O_RDWR, 0600);
  
  // Grow shmFd to needed data size
  int shmSize = sizeof(plrData_t) + nProc*sizeof(perProcData_t);
  if (ftruncate(shmFd, shmSize) == -1) {
    perror("ftruncate");
    return -1;
  }
  
  // mmap the full shared area
  plrShm = mmap(NULL, shmSize, PROT_READ | PROT_WRITE, MAP_SHARED, shmFd, 0);
  if (plrShm == MAP_FAILED) {
    perror("mmap");
    return -1;
  }
  // allProcShm is located right after plrShm
  allProcShm = (perProcData_t*)(plrShm+1);
  close(shmFd);
  
  // Initialize values in plrShm. Values not explicitly initalized here default
  // to zero because of ftruncate on shmFd.
  plrShm->nProc = nProc;
  pthread_mutex_init_pshared(&plrShm->lock);
  pthread_mutex_init_pshared(&plrShm->toolLock);
  
  return 0;
}

///////////////////////////////////////////////////////////////////////////////

int plrSD_acquireSharedData() {
  int shmFd = plrSD_openShmFile(O_RDWR, 0);
  
  // First mmap the common data area
  // Need it to know how many redundant processes there are + get access to the lock
  plrShm = mmap(NULL, sizeof(plrData_t), PROT_READ | PROT_WRITE, MAP_SHARED, shmFd, 0);
  if (plrShm == MAP_FAILED) {
    perror("mmap");
    return -1;
  }
  
  // Then remap to get the per-process data areas too
  // Can't use mremap because Pin doesn't seem to support it
  int shmSize = sizeof(plrData_t) + plrShm->nProc*sizeof(perProcData_t);
  if (munmap(plrShm, sizeof(plrData_t)) < 0) {
    perror("munmap");
    return -1;
  }
  plrShm = mmap(NULL, shmSize, PROT_READ | PROT_WRITE, MAP_SHARED, shmFd, 0);
  if (plrShm == MAP_FAILED) {
    perror("mmap");
    return -1;
  }
  // allProcShm is located right after plrShm
  allProcShm = (perProcData_t*)(plrShm+1);
  close(shmFd);

  return 0;
}

///////////////////////////////////////////////////////////////////////////////

int plrSD_cleanupSharedData() {
  char shmName[NAME_MAX];
  plrSD_getShmName(shmName);
  
  // Only need to unlink the shm
  // mmap'd area will be automatically freed after last process exits
  if (shm_unlink(shmName) < 0) {
    perror("shm_unlink");
    return -1;
  }
  
  return 0;
}

///////////////////////////////////////////////////////////////////////////////

int plrSD_initProcData(perProcData_t *procShm) {
  // Initialize values in procShm
  procShm->pid = getpid();
  procShm->waitIdx = -1;
  procShm->shmFd = -1;
  pthread_cond_init_pshared(&procShm->cond[0]);
  pthread_cond_init_pshared(&procShm->cond[1]);
  
  return 0;
}

///////////////////////////////////////////////////////////////////////////////

int plrSD_initProcDataAsCopy(perProcData_t *procShm, perProcData_t *src) {
  // Copy insidePLR state from parent
  // NOTE: Must keep this before plrSD_initProcData, which calls getpid()
  procShm->insidePLR = src->insidePLR;
  
  // Initialize values in procShm
  plrSD_initProcData(procShm);
  
  // Copy stored syscall arguments & other state from parent
  procShm->shmFd = src->shmFd;
  memcpy(&procShm->syscallArgs, &src->syscallArgs, sizeof(syscallArgs_t));
  
  return 0;
}

///////////////////////////////////////////////////////////////////////////////

int plrSD_freeProcData(perProcData_t *procShm) {
  if (procShm->pid == 0) {
    // Already free, nothing to do
    return 0;
  }
  
  // Wake up any waiting threads & destroy condition variables
  // BUG: Trying to call pthread_cond_destroy to cleanup the cond variables
  // seems to hang if a process was waiting in the cond and is then terminated
  // Since there are no resources to cleanup for a Linux cond var, better
  // off just skipping _destroy in lieu of a more complex solution
  //for (int i = 0; i < 2; ++i) {
  //  if (pthread_cond_broadcast(&procShm->cond[i]) != 0) {
  //    plrlog(LOG_ERROR, "pthread_cond_broadcast failed\n");
  //    return -1;
  //  }
  //  if (pthread_cond_destroy(&procShm->cond[i]) != 0) {
  //    plrlog(LOG_ERROR, "pthread_cond_destroy failed\n");
  //    return -1;
  //  }
  //}
  
  // Fill struct with zeros
  memset(procShm, 0, sizeof(perProcData_t));
  
  return 0;
}

///////////////////////////////////////////////////////////////////////////////

// Function to calculate & cache extraShm offset, to avoid both code duplication
// & unnecessary syscall and calculations when not updating extra shm

static int extraShmOffset = 0;
static int plrSD_extraShmOffset() {
  if (extraShmOffset == 0) {
    // Determine offset of extra shm area within shm file, must be page aligned
    int pageSize = sysconf(_SC_PAGE_SIZE);
    int fixedDataSize = sizeof(plrData_t) + plrShm->nProc*sizeof(perProcData_t);
    int rem = fixedDataSize % pageSize;
    extraShmOffset = (rem == 0) ? fixedDataSize : fixedDataSize + pageSize - rem;
  }
  return extraShmOffset;
}

///////////////////////////////////////////////////////////////////////////////

static int plrSD_openShmFile(int oflag, mode_t mode) {
  char shmName[NAME_MAX];
  plrSD_getShmName(shmName);
  int shmFd = shm_open(shmName, oflag, mode);
  if (shmFd < 0) {
    perror("shm_open");
    return -1;
  }
  return shmFd;
}

///////////////////////////////////////////////////////////////////////////////

int plrSD_resizeExtraShm(int minSize) {
  if (myProcShm->shmFd == -1) {
    myProcShm->shmFd = plrSD_openShmFile(O_RDWR, 0);
  }
  
  // Expand the global size of the extra shared memory area, if needed
  if (minSize > plrShm->extraShmSize) {
    plrShm->extraShmSize = minSize;
    
    // Grow shmFd to new max size
    int shmSize = plrSD_extraShmOffset() + minSize;
    if (ftruncate(myProcShm->shmFd, shmSize) == -1) {
      perror("ftruncate");
      return -1;
    }
  }
  
  return 0;
}

///////////////////////////////////////////////////////////////////////////////

int plrSD_refreshExtraShm() {
  if (myProcShm->shmFd == -1) {
    myProcShm->shmFd = plrSD_openShmFile(O_RDWR, 0);
  }
  
  // Expand the mapped size for this process, if needed
  if (myProcShm->extraShmMapped < plrShm->extraShmSize) {
    if (myProcShm->extraShmMapped == 0) {
      extraShm = mmap(NULL, plrShm->extraShmSize, PROT_READ | PROT_WRITE, MAP_SHARED, myProcShm->shmFd, plrSD_extraShmOffset());
      if (extraShm == MAP_FAILED) {
        perror("mmap");
        return -1;
      }
    } else {
      // Can't use mremap because Pin doesn't seem support it
      if (munmap(extraShm, myProcShm->extraShmMapped) < 0) {
        perror("munmap");
        return -1;
      }
      extraShm = mmap(NULL, plrShm->extraShmSize, PROT_READ | PROT_WRITE, MAP_SHARED, myProcShm->shmFd, plrSD_extraShmOffset());
      if (plrShm == MAP_FAILED) {
        perror("mmap");
        return -1;
      }
    }
    myProcShm->extraShmMapped = plrShm->extraShmSize;
  }
  
  return 0;
}

///////////////////////////////////////////////////////////////////////////////

static int plrSD_getShmName(char name[NAME_MAX]) {
  int pgid = getpgrp();
  int w = snprintf(name, NAME_MAX, "/plr_data.%d", pgid);
  if (w < 0 || w >= NAME_MAX) {
    return -1;
  }
  return 0;
}

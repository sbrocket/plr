// _GNU_SOURCE needed for mremap
#define _GNU_SOURCE
#include "plrSharedData.h"
#include "pthreadUtil.h"
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

///////////////////////////////////////////////////////////////////////////////
// Global data
plrData_t *plrShm = NULL;
perProcData_t *allProcShm = NULL;
perProcData_t *myProcShm = NULL;

///////////////////////////////////////////////////////////////////////////////
// Private functions
int plrSD_getShmName(char name[NAME_MAX]);

///////////////////////////////////////////////////////////////////////////////

int plrSD_initSharedData(int nProc) {
  char shmName[NAME_MAX];
  plrSD_getShmName(shmName);
  
  int shmFd = shm_open(shmName, O_CREAT | O_EXCL | O_RDWR, 0600);
  if (shmFd < 0) {
    perror("shm_open");
    return -1;
  }
  
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
  
  // Initialize values in plrShm. Values not explicitly initalized here default
  // to zero because of ftruncate on shmFd.
  plrShm->nProc = nProc;
  pthread_mutex_init_pshared(&plrShm->lock);
  pthread_mutex_init_pshared(&plrShm->toolLock);
  
  int pid = getpid();
  printf("[%d] Init shm w/ name = '%s'\n", pid, shmName);
  
  return 0;
}

///////////////////////////////////////////////////////////////////////////////

int plrSD_acquireSharedData() {
  char shmName[NAME_MAX];
  plrSD_getShmName(shmName);
  
  int shmFd = shm_open(shmName, O_RDWR, 0);
  if (shmFd < 0) {
    perror("shm_open");
    return -1;
  }
    
  // First mmap the common data area
  // Need it to know how many redundant processes there are + get access to the lock
  //int shmSize = sizeof(plrData_t) + 3*sizeof(perProcData_t);

  plrShm = mmap(NULL, sizeof(plrData_t), PROT_READ | PROT_WRITE, MAP_SHARED, shmFd, 0);
  //plrShm = mmap(NULL, shmSize, PROT_READ | PROT_WRITE, MAP_SHARED, shmFd, 0);
  if (plrShm == MAP_FAILED) {
    perror("mmap");
    return -1;
  }
  
  // Then mremap to get the per-process data areas too
  int shmSize = sizeof(plrData_t) + plrShm->nProc*sizeof(perProcData_t);
  if (munmap(plrShm, sizeof(plrData_t)) < 0) {
    perror("munmap");
    return -1;
  }
  plrShm = mmap(NULL, shmSize, PROT_READ | PROT_WRITE, MAP_SHARED, shmFd, 0);
  //plrShm = mremap(plrShm, sizeof(plrData_t), shmSize, MREMAP_MAYMOVE);
  if (plrShm == MAP_FAILED) {
    perror("mremap");
    return -1;
  }
  // allProcShm is located right after plrShm
  allProcShm = (perProcData_t*)(plrShm+1);

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
  pthread_cond_init_pshared(&procShm->cond[0]);
  pthread_cond_init_pshared(&procShm->cond[1]);
  
  return 0;
}

///////////////////////////////////////////////////////////////////////////////

int plrSD_initProcDataAsCopy(perProcData_t *procShm, perProcData_t *src) {
  // Initialize same things as plrSD_initProcData normally does
  plrSD_initProcData(procShm);
  
  // Copy stored syscall arguments to from parent
  memcpy(&procShm->syscallArgs, &src->syscallArgs, sizeof(syscallArgs_t));
  
  return 0;
}

///////////////////////////////////////////////////////////////////////////////

int plrSD_freeProcData(perProcData_t *procShm) {
  if (procShm->pid == 0) {
    // Already free, nothing to do
    return 0;
  }
  
  // Destroy existing condition variable
  for (int i = 0; i < 2; ++i) {
    if (pthread_cond_destroy(&procShm->cond[i]) != 0) {
      fprintf(stderr, "pthread_cond_destroy failed\n");
      return -1;
    }
  }
  
  // Fill struct with zeros
  memset(procShm, 0, sizeof(perProcData_t));
  
  return 0;
}

///////////////////////////////////////////////////////////////////////////////

int plrSD_getShmName(char name[NAME_MAX]) {
  int pgid = getpgrp();
  int w = snprintf(name, NAME_MAX, "/plr_data.%d", pgid);
  if (w < 0 || w >= NAME_MAX) {
    return -1;
  }
  return 0;
}

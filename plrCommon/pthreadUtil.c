#include "pthreadUtil.h"
#include <pthread.h>
#include <stdio.h>

int pthread_mutex_init_pshared(pthread_mutex_t *mutex) {
  int ret;
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  if ((ret = pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED)) != 0) {
    fprintf(stderr, "pthread_mutexattr_setpshared failed with code %d\n", ret);
    return ret;
  }
  if ((ret = pthread_mutex_init(mutex, &attr)) != 0) {
    fprintf(stderr, "pthread_mutexattr_init failed with code %d\n", ret);
    return ret;
  }
  pthread_mutexattr_destroy(&attr);
  return 0;
}

int pthread_cond_init_pshared(pthread_cond_t *cond) {
  int ret;
  pthread_condattr_t attr;
  pthread_condattr_init(&attr);
  if ((ret = pthread_condattr_setpshared(&attr, PTHREAD_PROCESS_SHARED)) != 0) {
    fprintf(stderr, "pthread_condattr_setpshared failed with code %d\n", ret);
    return ret;
  }
  if ((ret = pthread_cond_init(cond, &attr)) != 0) {
    fprintf(stderr, "pthread_condattr_init failed with code %d\n", ret);
    return ret;
  }
  pthread_condattr_destroy(&attr);
  return 0;
}

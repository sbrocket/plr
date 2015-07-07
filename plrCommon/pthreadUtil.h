#ifndef PTHREAD_UTIL_H
#define PTHREAD_UTIL_H
#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>

// Initializes a pthread_mutex_t with the PTHREAD_PROCESS_SHARED attribute set
int pthread_mutex_init_pshared(pthread_mutex_t *mutex);

// Initializes a pthread_cond_t with the PTHREAD_PROCESS_SHARED attribute set
int pthread_cond_init_pshared(pthread_cond_t *cond);

#ifdef __cplusplus
}
#endif
#endif

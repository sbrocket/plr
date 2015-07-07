#ifndef TIME_UTIL_H
#define TIME_UTIL_H
#ifdef __cplusplus
extern "C" {
#endif

#include <time.h>

struct timespec tspecNew(long int sec, long int nsec);
struct timespec tspecNewMs(long int msec);
struct timespec tspecAdd(const struct timespec tspec1, const struct timespec tspec2);
struct timespec tspecAddMs(const struct timespec tspec, long int msec);
// Note that tspecSub will return a negative timespec if tspec2 > tspec1
struct timespec tspecSub(const struct timespec tspec1, const struct timespec tspec2);
double tspecToFloat(const struct timespec tspec);

#ifdef __cplusplus
}
#endif
#endif

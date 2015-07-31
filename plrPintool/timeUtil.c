#include "timeUtil.h"
#include <time.h>

struct timespec tspecNew(long int sec, long int nsec) {
  sec = (sec < 0) ? 0 : sec;
  nsec = (nsec < 0) ? 0 : nsec;
  
  // If nsec > 1 billion, shift full seconds to sec value
  sec += nsec / 1000000000;
  nsec = nsec % 1000000000;
  
  struct timespec tspec = { .tv_sec = sec, .tv_nsec = nsec };
  return tspec;
}

struct timespec tspecNewMs(long int msec) {
  // If msec > 1000, shift full seconds to sec value
  long int sec = msec / 1000;
  long int nsec = (msec % 1000) * 1000000;
  
  struct timespec tspec = { .tv_sec = sec, .tv_nsec = nsec };
  return tspec;
}

struct timespec tspecAdd(const struct timespec tspec1, const struct timespec tspec2) {
  struct timespec res = {
    .tv_sec = tspec1.tv_sec + tspec2.tv_sec,
    .tv_nsec = tspec1.tv_nsec + tspec2.tv_nsec };
  
  // If nsec > 1 billion, shift full seconds to sec value
  res.tv_sec += res.tv_nsec / 1000000000;
  res.tv_nsec = res.tv_nsec % 1000000000; 
  return res;
}

struct timespec tspecAddMs(const struct timespec tspec, long int msec) {
  struct timespec addVal = { .tv_nsec = msec*1000000 };
  return tspecAdd(tspec, addVal);
}

struct timespec tspecSub(const struct timespec tspec1, const struct timespec tspec2) {
  struct timespec res = {
    .tv_sec = tspec1.tv_sec - tspec2.tv_sec,
    .tv_nsec = tspec1.tv_nsec - tspec2.tv_nsec };
  
  // If abs(nsec) > 1 billion, shift full seconds to sec value
  res.tv_sec += res.tv_nsec / 1000000000;
  res.tv_nsec = res.tv_nsec % 1000000000; 
  return res;
}

double tspecToFloat(const struct timespec tspec) {
  return (double)tspec.tv_sec + (tspec.tv_nsec / 1000000000.0);
}

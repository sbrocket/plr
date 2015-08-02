#ifndef PLR_LOG_H
#define PLR_LOG_H
#ifdef __cplusplus
extern "C" {
#endif

// Note that LOG_NONE is not a valid level to use for the functions below
typedef enum {
  LOG_NONE = 0,
  LOG_ERROR,
  LOG_SYSCALL,
  LOG_DEBUG,
  LOG_ALL,
  LOG_DEFAULT = LOG_ERROR
} plrLogLevel_t;

__attribute__((format(gnu_printf, 2, 3)))
int plrlog(plrLogLevel_t level, const char *format, ...);

// Returns 1 if a message at level would get output, 0 otherwise
int plrlogIsEnabled(plrLogLevel_t level);

#ifdef __cplusplus
}
#endif
#endif

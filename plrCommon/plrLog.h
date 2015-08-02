#ifndef PLR_LOG_H
#define PLR_LOG_H
#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif
#endif

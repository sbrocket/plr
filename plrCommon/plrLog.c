#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include "plrLog.h"

static plrLogLevel_t g_logLevel = -1;
static FILE *g_logFile;

void checkLogSettings() {
  if ((int)g_logLevel >= 0) {
    return;
  }
  
  char *levelStr = getenv("PLR_LOGLEVEL");
  if (levelStr) {
    char *endptr;
    long val = strtol(levelStr, &endptr, 10);
    if (endptr == levelStr || *endptr != '\0' || ((val == LONG_MIN || val == LONG_MAX) && errno == ERANGE)) {
      fprintf(stderr, "Error: PLR_LOGLEVEL ('%s') is not an integer value\n", levelStr);
    } else {
      if (val < LOG_NONE || val > LOG_ALL) {
        fprintf(stderr, "Error: PLR_LOGLEVEL (%ld) is an out-of-range log level\n", val);
      } else {
        g_logLevel = (plrLogLevel_t)val;
      }
    }
  }
  if ((int)g_logLevel < 0) {
    g_logLevel = LOG_DEFAULT;
  }
  
  g_logFile = stderr;
}

__attribute__((format(gnu_printf, 2, 3)))
int plrlog(plrLogLevel_t level, const char *format, ...) {
  checkLogSettings();
  
  // plrlog calls shouldn't specify LOG_NONE as the message level
  if (level == LOG_NONE) {
    assert(level != LOG_NONE);
    return 0;
  }
  
  if (level <= g_logLevel) {
    int ret;
    va_list argp;
    va_start(argp, format);
    ret = vfprintf(g_logFile, format, argp);
    va_end(argp);
    return ret;
  }
  return 0;
}
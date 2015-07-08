#ifndef CRC32C_UTIL_H
#define CRC32C_UTIL_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

int setupCRC32c();
int calcCRC32c(const void *buf, size_t len);

#ifdef __cplusplus
}
#endif
#endif

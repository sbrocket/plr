#ifndef CRC32_UTIL_H
#define CRC32_UTIL_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

// crc32c calculation using Linux kernel crypto AF_ALG interface
int setup_crc32c();
int crc32c(const void *buf, size_t len, unsigned int *result);

// crc32 calculation using table lookup
uint32_t crc32(uint32_t crc, const void *buf, size_t size);

#ifdef __cplusplus
}
#endif
#endif

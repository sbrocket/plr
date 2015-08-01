#ifndef CRC32_UTIL_H
#define CRC32_UTIL_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

// crc32 calculation using table lookup
uint32_t crc32(uint32_t crc, const void *buf, size_t size);

#ifdef __cplusplus
}
#endif
#endif

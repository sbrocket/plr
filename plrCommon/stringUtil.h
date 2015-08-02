#ifndef STRING_UTIL_H
#define STRING_UTIL_H
#ifdef __cplusplus
extern "C" {
#endif

// Replaces characters with common C escape sequences with said sequence.
// For example, newlines in a string are turned into "\n", tabs into "\t", etc.
// Return value is a malloc'd string, caller must free() it to prevent leak
char *str_expandEscapes(const char *src);

#ifdef __cplusplus
}
#endif
#endif

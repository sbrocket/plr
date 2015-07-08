#ifndef PLR_COMPARE_H
#define PLR_COMPARE_H
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  // Address of libc syscall function, as offset from start of shared library
  void *addr;
  // Syscall arguments represented as integers
  // Longer arguments (like strings or buffers) are hashed
  unsigned long arg[6];
} syscallArgs_t;

// Compares the contents of two syscallArgs_t structs
// Return value:
//      0 : Arguments are identical
//   >= 1 : Arguments are different
int plrC_compareArgs(const syscallArgs_t *args1, const syscallArgs_t *args2);

#ifdef __cplusplus
}
#endif
#endif

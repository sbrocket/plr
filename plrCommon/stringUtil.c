#include <stdlib.h>
#include <string.h>
#include "stringUtil.h"

char *str_expandEscapes(const char *src) {
  char *dest = malloc(2*strlen(src) + 1);
  char *ret = dest;
  char c;
  while ((c = *(src++))) {
    switch(c) {
      case '\a':
        *(dest++) = '\\';
        *(dest++) = 'a';
        break;
      case '\b':
        *(dest++) = '\\';
        *(dest++) = 'b';
        break;
      case '\t':
        *(dest++) = '\\';
        *(dest++) = 't';
        break;
      case '\n':
        *(dest++) = '\\';
        *(dest++) = 'n';
        break;
      case '\v':
        *(dest++) = '\\';
        *(dest++) = 'v';
        break;
      case '\f':
        *(dest++) = '\\';
        *(dest++) = 'f';
        break;
      case '\r':
        *(dest++) = '\\';
        *(dest++) = 'r';
        break;
      case '\\':
        *(dest++) = '\\';
        *(dest++) = '\\';
        break;
      case '\"':
        *(dest++) = '\\';
        *(dest++) = '\"';
        break;
      default:
        *(dest++) = c;
     }
  }
  // Ensure string terminator
  *dest = '\0';
  return ret;
}

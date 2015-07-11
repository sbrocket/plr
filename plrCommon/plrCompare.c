#include "plrCompare.h"
#include <stdio.h>

int plrC_compareArgs(const syscallArgs_t *args1, const syscallArgs_t *args2) {
  int faultVal = 0;
  
  #define CompareElement(elem, faultBit)    \
  if (args1->elem != args2->elem) {         \
    faultVal |= 1 << faultBit;              \
    printf("Argument miscompare in " #elem ", 0x%lX != 0x%lX\n",  \
      (unsigned long)args1->elem, (unsigned long)args2->elem);    \
  }
  
  CompareElement(addr,   0);
  CompareElement(arg[0], 1);
  CompareElement(arg[1], 2);
  CompareElement(arg[2], 3);
  CompareElement(arg[3], 4);
  CompareElement(arg[4], 5);
  CompareElement(arg[5], 6);
  
  return faultVal;
}

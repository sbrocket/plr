#include "plrCompare.h"

int plrC_compareArgs(const syscallArgs_t *args1, const syscallArgs_t *args2) {
  int foundDiff = 0;
  if      (args1->addr   != args2->addr)   { foundDiff = 1; }
  else if (args1->arg[0] != args2->arg[0]) { foundDiff = 2; }
  else if (args1->arg[1] != args2->arg[1]) { foundDiff = 3; }
  else if (args1->arg[2] != args2->arg[2]) { foundDiff = 4; }
  else if (args1->arg[3] != args2->arg[3]) { foundDiff = 5; }
  else if (args1->arg[4] != args2->arg[4]) { foundDiff = 6; }
  else if (args1->arg[5] != args2->arg[5]) { foundDiff = 7; }
  return foundDiff;
}

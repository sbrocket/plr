#include <stdio.h>
#include <stdlib.h>
#include "plr.h"

__attribute__((constructor))
void initPLRPreload() {
  if (plr_processInit() < 0) {
    fprintf(stderr, "Error: PLR process init failed\n");
    exit(1);
  }
}

__attribute__((destructor))
void cleanupPLRPreload() {
  
}

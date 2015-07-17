#include <stdio.h>
#include <stdlib.h>
#include "plr.h"
#include "crc32_util.h"

__attribute__((constructor))
void initPLRPreload() {
  if (plr_processInit() < 0) {
    fprintf(stderr, "Error: PLR process init failed\n");
    exit(1);
  }
  if (setup_crc32c() < 0) {
    fprintf(stderr, "Error: crc32c setup failed\n");
    exit(1);
  }
}

__attribute__((destructor))
void cleanupPLRPreload() {
  
}

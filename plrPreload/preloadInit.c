#include <stdio.h>
#include <stdlib.h>
#include "plr.h"
#include "crc32c_util.h"

__attribute__((constructor))
void initPLRPreload() {
  if (plr_processInit() < 0) {
    fprintf(stderr, "Error: PLR process init failed\n");
    exit(1);
  }
  if (setupCRC32c() < 0) {
    fprintf(stderr, "Error: CRC32c setup failed\n");
    exit(1);
  }
}

__attribute__((destructor))
void cleanupPLRPreload() {
  
}

#include "crc32c_util.h"
#include <sys/socket.h>
#include <linux/if_alg.h>
#include <unistd.h>
#include <stddef.h>

// CRC2c calculation using kernel crypto based on code snippet from:
// http://stackoverflow.com/questions/6111882/how-to-use-kernel-libcrc32cor-same-functions-in-userspace-programmes

// Global data
static int sockfd[2] = { -1, -1 };

int setupCRC32c() {
  if (sockfd[0] != -1 && sockfd[1] != -1) {
    return 0;
  }
  
  struct sockaddr_alg sa = {
    .salg_family = AF_ALG,
    .salg_type   = "hash",
    .salg_name   = "crc32c"
  };

  if ((sockfd[0] = socket(AF_ALG, SOCK_SEQPACKET, 0)) == -1) {
    return -1;
  }
  if (bind(sockfd[0], (struct sockaddr*)&sa, sizeof(sa)) != 0) {
    return -1;
  }
  if ((sockfd[1] = accept(sockfd[0], NULL, 0)) == -1) {
    return -1;
  }
  return 0;
}

int calcCRC32c(const void *buf, size_t len) {
  if (sockfd[0] == -1 || sockfd[1] == -1) {
    return -1;
  }
  
  if (send(sockfd[1], buf, len, MSG_MORE) != (ssize_t)len) {
    return -1;
  }

  int crc32c;
  if (read(sockfd[1], &crc32c, 4) != 4) {
    return -1;
  }
  return crc32c;
}

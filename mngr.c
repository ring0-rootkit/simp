#include "mngr.h"
#include "defines.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define handle_error(msg) \
         do { perror(msg); exit(EXIT_FAILURE); } while (0)

int a[MAX_PORT_NUMBER];

int handle_message_ready_signal() {
  return 0;
}

int validate_port() {
  return 0;
}

int extract_port() {
  return 0;
}

int read_from_pipe() {
  return 0;
}

int write_to_pipe() {
  return 0;
}

int main(void) {

  int sockfd = socket(AF_INET, SOCK_RAW, 254);
  if (sockfd == -1)
    handle_error("socket");

  struct sockaddr_in addr;
  struct sockaddr_in addr_from;
  int addr_from_len = 0;
  memset(&addr, 0, sizeof(addr));
  memset(&addr_from, 0, sizeof(addr_from));

  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = 0;

  if(bind(sockfd, (struct sockaddr *) &addr, sizeof(addr)) == -1)
    handle_error("bind");

  // if(listen(sockfd, MAX_PORT_NUMBER) == -1)
  //   handle_error("listen");

  //register signal handler

  void* buf = malloc(MAX_BUF_LEN);

  while(1) {
    int n = recvfrom(sockfd, buf, MAX_BUF_LEN, 0,
        (struct sockaddr *)&addr_from, (socklen_t *)&addr_from_len);
    if(n == -1)
      handle_error("recv");

    fwrite(buf, 1, n, stdout);

    // extract_port();
    // write_to_pipe();
  }

  return 0;
}

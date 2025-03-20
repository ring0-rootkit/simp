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

int main(void) {

  int sockfd = socket(AF_INET, SOCK_RAW, 254);
  if (sockfd == -1)
    handle_error("socket");

  struct sockaddr_in addr;
  struct sockaddr_in addr_dest;
  memset(&addr, 0, sizeof(addr));
  memset(&addr_dest, 0, sizeof(addr_dest));

  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = 0;

  addr_dest.sin_family = AF_INET;
  addr_dest.sin_addr.s_addr = 0;

  if(bind(sockfd, (struct sockaddr *) &addr, sizeof(addr)) == -1)
    handle_error("bind");

  char* buf = "test";


  while(1) {
    int n = sendto(sockfd, buf, strlen(buf), 0, (struct sockaddr *) &addr_dest,
        sizeof(addr_dest));
    if(n == -1)
      handle_error("send");
  }

  return 0;
}

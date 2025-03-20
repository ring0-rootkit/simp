#include "grtp.h"

int main() {

  int err = grtp_connect();
  if (err != OK) {
    printf("could not connect to the socket");
    return 1;
  }
}

#include "simp.h"
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>

int main(void) {
  simp_context_t *ctx = simp_new();
  if (!ctx) {
    fprintf(stderr, "Failed to create shared memory context\n");
    return 1;
  }

  int err = simp_init(ctx, "127.0.0.1", 5000);
  if (err) {
    perror("cannot init");
    simp_cleanup(ctx);
    return 1;
  }
  printf("init\n");

  err = simp_start(ctx);
  if (err) {
    perror("failed to start");
    simp_cleanup(ctx);
    return 1;
  }

  char buf[128];
  int len = simp_recv(ctx, buf, 128);
  if (len < 0) {
    perror("read");
    simp_cleanup(ctx);
    return 1;
  }
  printf("packet 1: %.*s\n", len, buf);
  len = simp_recv(ctx, buf, 128);
  if (len < 0) {
    perror("read");
    simp_cleanup(ctx);
    return 1;
  }
  printf("packet2: %.*s\n", len, buf);

  simp_cleanup(ctx);
  
  return 0;
}

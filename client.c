#include "simp.h"
#include <signal.h>
#include <fcntl.h>
#include <pthread.h>

int main(void) {
  simp_context_t *ctx = simp_new();
  if (!ctx) {
    fprintf(stderr, "Failed to create shared memory context\n");
    return 1;
  }

  printf("pre init\n");
  int err = simp_init(ctx, "127.0.0.1", 5001);
  if (err) {
    perror("cannot init");
    simp_cleanup(ctx);
    return 1;
  }
  printf("init\n");

  err = simp_connect(ctx, "127.0.0.1", 5000);
  if (err) {
    perror("cannot connect");
    simp_cleanup(ctx);
    return 1;
  }

  printf("before send\n");
  err = simp_send(ctx, (const uint8_t *)"Test", 4, PRIO_HIGH, 0);
  if (err < 0) {
    perror("send");
    simp_cleanup(ctx);
    return 1;
  }
  printf("after send\n");
  simp_cleanup(ctx);
}

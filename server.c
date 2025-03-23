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
  while(1) {};
  
  return 0;
}

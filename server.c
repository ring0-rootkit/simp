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

  int err = simp_init(ctx, "127.0.0.1", 6000);
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
  for (int i = 0; i < 1000; i++) {
    int len = simp_recv(ctx, buf, 128);
    if (len < 0) {
      perror("read");
      simp_cleanup(ctx);
      return 1;
    }
    if (i != atoi(buf)) {
      printf("messages are out of order!\n");
      printf("recieved: %d, expected: %d\n", atoi(buf), i);
      simp_cleanup(ctx);
      return 1;
    }
    printf("%.*s\n", len, buf);
  }

  simp_cleanup(ctx);
  
  return 0;
}

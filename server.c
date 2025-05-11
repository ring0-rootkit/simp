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

  int err = simp_init(ctx, "0.0.0.0", 5000);
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
  int counter = 0;
  for (int i = 0; i < 1000; i++) {
    int len = simp_recv(ctx, buf, 128);
    if (len < 0) {
      perror("read");
      simp_cleanup(ctx);
      return 1;
    }
    counter++;
    if (strcmp(buf, "999") == 0) {
      break;
    }
    printf("%3s\n", buf);
  }

  printf("received %d messages\n", counter);

  simp_cleanup(ctx);
  
  return 0;
}

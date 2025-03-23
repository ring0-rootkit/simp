#include "simp.h"

int main(void) {
  printf("pre init");
  simp_context_t ctx;
  printf("pre init");
  int err = simp_init(&ctx, "127.0.0.1", 5000);
  if (err) {
    perror("cannot init");
  }
  printf("init");
  err = simp_start(&ctx);
  if (err) {
    perror("start");
  }
  printf("start");

  while(1) {};

  char buf[11];
  err = simp_receive(&ctx, buf, 11);
  if (err < 0) {
    perror("cannot receive data");
  }
  printf("%11s", buf);

  return 0;
}

#include "simp.h"

int main(void) {
  simp_context_t ctx;
  printf("pre init");
  int err = simp_init(&ctx, "127.0.0.1", 5001);
  if (err) {
    perror("cannot init");
  }
  printf("init");
  err = simp_connect(&ctx, "127.0.0.1", 5000);
  if (err) {
    perror("cannot connect");
  }
  printf("connect");

  while(1) {};
  
  err = simp_send(&ctx, (uint8_t*)"Important", 9, PRIO_HIGH, 0);
  if (err < 0) {
    perror("cannot send");
  }
  err = simp_send(&ctx, (uint8_t*)"Update", 6, PRIO_MEDIUM, 1);
  if (err < 0) {
    perror("cannot send");
  }
  err = simp_send(&ctx, (uint8_t*)"New Update", 10, PRIO_MEDIUM, 1);
  if (err < 0) {
    perror("cannot send");
  }
  err = simp_send(&ctx, (uint8_t*)"Temp", 4, PRIO_LOW, 0);
  if (err < 0) {
    perror("cannot send");
  }
  return 0;
}

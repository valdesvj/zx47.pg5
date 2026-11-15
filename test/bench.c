/* bench.c - how fast the 48K machine runs on the PC: a BASIC loop that never halts (make bench).
 * Only the ratio between versions matters (the calculator is ~20-60x slower). GPL-3.0-or-later. */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "zx.h"

static Zx X;
static void frames(int n) { for (int i = 0; i < n; i++) zx_frame(&X); }
static void press(int a, int b) { zx_key(&X, a, 1); zx_key(&X, b, 1); frames(4); zx_keys_clear(&X); frames(4); }

int main(int argc, char **argv) {
  FILE *f = fopen(argc > 1 ? argv[1] : "build/bench/busy.tap", "rb");
  if (!f || zx_init(&X)) return 1;
  static uint8_t tap[4096];
  uint32_t n = (uint32_t)fread(tap, 1, sizeof tap, f);
  fclose(f);
  frames(150);
  zx_tape(&X, tap, n);
  press(ZK_J, ZK_NONE); press(ZK_SYM, ZK_P); press(ZK_SYM, ZK_P); press(ZK_ENTER, ZK_NONE);
  frames(50);
  struct timespec a, b;
  int N = 3000;
  clock_gettime(CLOCK_MONOTONIC, &a);
  frames(N);
  clock_gettime(CLOCK_MONOTONIC, &b);
  double us = ((b.tv_sec - a.tv_sec) * 1e6 + (b.tv_nsec - a.tv_nsec) / 1e3) / N;
  printf("%.1f us a frame (%d frames)\n", us, N);
  return 0;
}

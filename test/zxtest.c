/* zxtest.c - the 48K machine (src/zx.c) on the PC, no calculator parts:
 *   make test-zx
 * 1. the ROM boots: after 3 s the bottom line reads (C) 1982 Sinclair Research Ltd (checked by the font in the ROM)
 * 2. keys: typing PRINT 2+3 ENTER prints 5
 * 3. the tape: test/demo.tap (made by tools/mktap.py from test/demo.bas) loads with LOAD "" and runs
 * 4. .SNA: save, load back, the machine goes on the same
 * 5. .Z80: version 1 packed and version 3 pages (made by tools/sna2z80.py) load the same memory
 * zxtest --sna FILE: only boots, loads the tape, runs it and saves FILE (the .Z80 files are made from it).
 * Writes screens as build/test/NAME.pbm (1 = ink) for a look. GPL-3.0-or-later. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zx.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } \
                           else { printf("ok   "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static Zx X;

static void frames(int n) { for (int i = 0; i < n; i++) zx_frame(&X); }

static void press(int k1, int k2, int n) {
  zx_key(&X, k1, 1); if (k2 != ZK_NONE) zx_key(&X, k2, 1);
  frames(n);
  zx_keys_clear(&X);
  frames(n);
}

/* screen text: each 8x8 cell compared with the ROM font (0x3D00, chars 32..127; 0x7F is the (C) sign) */
static void screen_line(int row, char *out) {
  for (int col = 0; col < 32; col++) {
    char c = '?';
    for (int ch = 32; ch < 128; ch++) {
      int same = 1, inv = 1;
      for (int l = 0; l < 8; l++) {
        int y = row * 8 + l;
        uint16_t a = (uint16_t)(0x4000 | ((y & 0xC0) << 5) | ((y & 7) << 8) | ((y & 0x38) << 2) | col);
        uint8_t f = zx_rom48[0x3D00 + (ch - 32) * 8 + l];
        if (X.mem[a] != f) same = 0;
        if (X.mem[a] != (uint8_t)~f) inv = 0;
      }
      if (same || inv) { c = ch == 0x7F ? '@' : (char)ch; break; }
    }
    out[col] = c;
  }
  out[32] = 0;
}

static void pbm(const char *name) {
  char fn[128];
  snprintf(fn, sizeof fn, "build/test/%s.pbm", name);
  FILE *f = fopen(fn, "w");
  if (!f) return;
  fprintf(f, "P1\n256 192\n");
  for (int y = 0; y < 192; y++) {
    for (int x = 0; x < 256; x++) {
      uint16_t a = (uint16_t)(0x4000 | ((y & 0xC0) << 5) | ((y & 7) << 8) | ((y & 0x38) << 2) | (x >> 3));
      fputc(X.mem[a] >> (7 - (x & 7)) & 1 ? '1' : '0', f);
    }
    fputc('\n', f);
  }
  fclose(f);
}

static uint8_t *slurp(const char *fn, uint32_t *n) {
  FILE *f = fopen(fn, "rb");
  if (!f) { *n = 0; return NULL; }
  fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET);
  uint8_t *b = malloc((size_t)l);
  *n = (uint32_t)fread(b, 1, (size_t)l, f);
  fclose(f);
  return b;
}

static int find_text(const char *s) {
  char line[33];
  for (int r = 0; r < 24; r++) { screen_line(r, line); if (strstr(line, s)) return r; }
  return -1;
}

static void dump(void) { char line[33]; for (int r = 0; r < 24; r++) { screen_line(r, line); printf("   |%s|\n", line); } }

static int make_sna(const char *out) {
  uint32_t tn;
  uint8_t *tap = slurp("build/test/demo.tap", &tn);
  if (!tap) return 1;
  frames(150);
  zx_tape(&X, tap, tn);
  press(ZK_J, ZK_NONE, 4); press(ZK_SYM, ZK_P, 4); press(ZK_SYM, ZK_P, 4); press(ZK_ENTER, ZK_NONE, 4);
  frames(150);
  static uint8_t sna[ZX_SNA_SIZE];
  zx_save_sna(&X, sna);
  FILE *f = fopen(out, "wb");
  if (!f) return 1;
  fwrite(sna, 1, sizeof sna, f);
  fclose(f);
  return 0;
}

int main(int argc, char **argv) {
  if (zx_init(&X)) { printf("no memory\n"); return 1; }
  if (argc == 3 && !strcmp(argv[1], "--sna")) return make_sna(argv[2]);

  /* 1 boot */
  frames(150);
  pbm("boot");
  CHECK(find_text("@ 1982 Sinclair Research Ltd") == 23, "the ROM boots to (C) 1982 Sinclair Research Ltd");

  /* 2 keys: P (PRINT in K mode), 2, SYMBOL SHIFT+K (+), 3, ENTER */
  press(ZK_P, ZK_NONE, 4); press(ZK_2, ZK_NONE, 4); press(ZK_SYM, ZK_K, 4); press(ZK_3, ZK_NONE, 4);
  press(ZK_ENTER, ZK_NONE, 4);
  frames(20);
  pbm("print");
  char l0[33]; screen_line(0, l0);
  CHECK(l0[0] == '5', "PRINT 2+3 gives 5 (top line: |%s|)", l0);
  int ok_line = find_text("0 OK, 0:1");
  CHECK(ok_line >= 0, "0 OK, 0:1 report");

  /* 3 tape */
  uint32_t tn;
  uint8_t *tap = slurp("build/test/demo.tap", &tn);
  CHECK(tap != NULL, "test tape build/test/demo.tap (%u bytes)", tn);
  if (tap) {
    zx_reset(&X); frames(150);
    zx_tape(&X, tap, tn);
    press(ZK_J, ZK_NONE, 4); press(ZK_SYM, ZK_P, 4); press(ZK_SYM, ZK_P, 4); press(ZK_ENTER, ZK_NONE, 4);
    frames(150);
    pbm("tape");
    CHECK(X.tap_blocks == 2, "LOAD \"\" read %d blocks (header + program)", X.tap_blocks);
    CHECK(find_text("ZX47 ON THE DM42n") >= 0, "the program ran: ZX47 ON THE DM42n on screen");
    if (find_text("ZX47 ON THE DM42n") < 0) dump();
  }

  /* 4 SNA round trip: save, run on 50 frames, load, run 50 frames: the same memory */
  static uint8_t sna[ZX_SNA_SIZE], m1[49152];
  zx_save_sna(&X, sna);
  frames(50);
  memcpy(m1, X.mem + 0x4000, 49152);
  int e = zx_load_sna(&X, sna, sizeof sna);
  frames(50);
  CHECK(e == 0 && !memcmp(m1, X.mem + 0x4000, 49152), ".SNA saved and loaded: the machine runs on the same");

  /* 5 Z80 v1 and v3, made from the .SNA by tools/sna2z80.py */
  const char *zf[2] = { "build/test/demo_v1.z80", "build/test/demo_v3.z80" };
  for (int i = 0; i < 2; i++) {
    uint32_t n;
    uint8_t *z = slurp(zf[i], &n);
    if (!z) { CHECK(0, "%s missing", zf[i]); continue; }
    e = zx_load_sna(&X, sna, sizeof sna);
    frames(50);
    memcpy(m1, X.mem + 0x4000, 49152);
    e = zx_load_z80(&X, z, n);
    frames(50);
    CHECK(e == 0 && !memcmp(m1, X.mem + 0x4000, 49152), "%s (%u bytes) loads and runs the same", zf[i], n);
    free(z);
  }

  /* 6 a 128K snapshot is refused politely */
  static uint8_t big[ZX_SNA_SIZE + 4 + 5 * 16384];
  CHECK(zx_load_sna(&X, big, sizeof big) == ZXE_128K, "128K .SNA refused: %s", zx_error(ZXE_128K));

  printf(fails ? "zx: %d FAILED\n" : "zx: all passed\n", fails);
  return fails != 0;
}

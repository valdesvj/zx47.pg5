/* z80test.c - the Z80 core on the PC with the classic instruction exercisers (CP/M programs, test/zex):
 *   make test-z80        prelim + zexdoc (a few minutes)
 *   make test-z80 ZEX=all  zexall too
 * A minimal CP/M: the program loads at 0x100, BDOS calls (CALL 5) for C=2 (char) and C=9 (string) are printed,
 * a jump to 0 ends it. Passes when the output has no "ERROR". GPL-3.0-or-later. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "z80.h"

static uint8_t mem[65536];
static int done, errors;
static char out[1 << 16];
static int nout;

static uint8_t in(z80 *z, uint16_t p) {
  (void)p;
  if (z->c == 2) { putchar(z->e); out[nout++ & 0xFFFF] = (char)z->e; }
  else if (z->c == 9) for (uint16_t a = (uint16_t)((z->d << 8) | z->e); mem[a] != '$'; a++) {
    putchar(mem[a]); out[nout++ & 0xFFFF] = (char)mem[a];
  }
  fflush(stdout);
  return 0xFF;
}
static void out_(z80 *z, uint16_t p, uint8_t v) { (void)z; (void)p; (void)v; done = 1; }

static int run(const char *file, unsigned long expect_cyc) {
  FILE *f = fopen(file, "rb");
  if (!f) { printf("cannot open %s\n", file); return 1; }
  memset(mem, 0, sizeof mem);
  size_t n = fread(mem + 0x100, 1, 0x10000 - 0x100, f);
  fclose(f);
  (void)n;
  /* 0x0000: OUT (0),A ends; 0x0005: IN A,(0) does the BDOS call, RET */
  mem[0] = 0xD3; mem[1] = 0x00;
  mem[5] = 0xDB; mem[6] = 0x00; mem[7] = 0xC9;
  z80 z;
  z80_init(&z);
  z.mem = mem; z.port_in = in; z.port_out = out_;     /* wlimit 0: all memory writable */
  z.pc = 0x100;
  done = 0; nout = 0;
  printf("*** %s\n", file);
  unsigned long steps = 0;
  while (!done) { z80_step(&z); steps++; }
  printf("\n*** %lu instructions, %lu cycles\n", steps, z.cyc);
  out[nout & 0xFFFF] = 0;
  int bad = strstr(out, "ERROR") != NULL || strstr(out, "rror") != NULL;
  if (expect_cyc && z.cyc != expect_cyc) { printf("cycles: expected %lu\n", expect_cyc); bad = 1; }
  return bad;
}

int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    unsigned long expect = strstr(argv[i], "prelim") ? 8721 : 0;
    if (run(argv[i], expect)) errors++;
  }
  printf(errors ? "Z80: %d test file(s) FAILED\n" : "Z80: all passed\n", errors);
  return errors != 0;
}

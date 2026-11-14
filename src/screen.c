/* screen.c - ZX47: the Spectrum's screen on the DM42n's LCD. See screen.h.
 *
 * Each Spectrum line becomes 32 bytes of black-and-white pixels:
 *   colour mode  in each 8x8 cell the darker of its two colours (ink, paper) is black, the lighter white (by the
 *                colours' brightness); a cell of one colour is all black or all white; FLASH swaps them every 16
 *                frames. Light drawings on black (Knight Lore) stay light on black, as on a TV.
 *   ink mode     every set pixel is black, whatever its colours (clearer for some games)
 *   invert       swaps black and white in both
 * A line goes to the LCD only when it changed since it was last sent (FPS47: about 0.25 ms a line, so a sprite in
 * a still room costs a few ms, the whole screen about 50 ms). The LCD buffer's layout is learnt at start by setting
 * single pixels (as FPS47 and NAV47's USB mirror); when each group of 8 pixels lands in one byte, a 256-entry table
 * converts a byte at a time.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <main.h>
#include <dmcp.h>
#include <stdlib.h>
#include <string.h>
#include "screen.h"

#define CELLS (SCR_W / 8)

static uint8_t (*last)[CELLS];          /* the lines on the LCD (heap, 6 KB) */
static uint8_t *dirty;                  /* SCR_H flags: this line changed */
static int valid;                       /* last[] holds what the LCD shows */

static uint8_t xbyte[SCR_W], xbit[SCR_W];   /* pixel -> byte and bit mask in lcd_line_addr() */
static int ink;                             /* the bit value of a black pixel */
static int fast;                            /* 8 pixels -> one byte everywhere, the same bit order */
static uint8_t gbyte[CELLS];                /* fast: the byte of each group of 8 */
static uint8_t lut[256];                    /* fast: 8 pixels (bit 7 = left, 1 = black) -> the LCD byte */
static uint8_t cell_and[256], cell_xor[256];/* colour mode, by attribute: black = (pixels & and) ^ xor */

/* ------------------------------------------------------------------ the LCD buffer's layout */
static int find_bit(int x, int y, int *byte, int *bit, int *val) {
  uint8_t a[LCD_LINE_BUF_SIZE], b[LCD_LINE_BUF_SIZE];
  lcd_fill_rect((uint32_t)x, (uint32_t)y, 1, 1, 1);
  memcpy(a, lcd_line_addr(y), sizeof a);
  lcd_fill_rect((uint32_t)x, (uint32_t)y, 1, 1, 0);
  memcpy(b, lcd_line_addr(y), sizeof b);
  for (int i = 0; i < (int)sizeof a; i++) {
    uint8_t d = (uint8_t)(a[i] ^ b[i]);
    if (!d) continue;
    for (int k = 0; k < 8; k++)
      if (d == (1u << k)) { *byte = i; *bit = k; *val = (a[i] >> k) & 1; return 1; }
    return 0;
  }
  return 0;
}

static int learn(void) {
  int y = SCR_Y, byte, bit, val;
  for (int x = 0; x < SCR_W; x++) {
    if (!find_bit(SCR_X + x, y, &byte, &bit, &val)) return 0;
    xbyte[x] = (uint8_t)byte;
    xbit[x] = (uint8_t)(1u << bit);
    if (x == 0) ink = val;
    else if (val != ink) return 0;
  }
  fast = 1;                                         /* each group of 8 in one byte, the same order? */
  for (int g = 0; g < CELLS && fast; g++) {
    gbyte[g] = xbyte[g * 8];
    for (int j = 0; j < 8; j++)
      if (xbyte[g * 8 + j] != gbyte[g] || xbit[g * 8 + j] != xbit[j]) fast = 0;
  }
  if (fast)
    for (int v = 0; v < 256; v++) {
      uint8_t o = 0;
      for (int j = 0; j < 8; j++) {
        int black = v >> (7 - j) & 1;
        if (black == ink) o |= xbit[j];
      }
      lut[v] = o;
    }
  return 1;
}

/* ------------------------------------------------------------------ colours to black and white */
static int lum(int c, int bright) {                 /* 0..255: c = 0 black .. 7 white (G R B bits) */
  int l = (c & 4 ? 587 : 0) + (c & 2 ? 299 : 0) + (c & 1 ? 114 : 0);   /* green, red, blue */
  return l * (bright ? 255 : 215) / 1000;
}

static void make_cells(void) {
  for (int a = 0; a < 256; a++) {
    int br = a & 0x40, li = lum(a & 7, br), lp = lum((a >> 3) & 7, br);
    if (li < lp) { cell_and[a] = 0xFF; cell_xor[a] = 0x00; }          /* ink darker: set pixels black */
    else if (li > lp) { cell_and[a] = 0xFF; cell_xor[a] = 0xFF; }     /* paper darker: clear pixels black */
    else { cell_and[a] = 0x00; cell_xor[a] = li < 128 ? 0xFF : 0x00; }/* one colour: all black or all white */
  }
}

int screen_init(void) {
  last = malloc(SCR_H * CELLS);
  dirty = malloc(SCR_H);
  if (!last || !dirty) return -1;
  make_cells();
  if (!learn()) { fast = 0; ink = 0; for (int x = 0; x < SCR_W; x++) { xbyte[x] = 0; xbit[x] = 0; } return -2; }
  valid = 0;
  return 0;
}

int screen_fast(void) { return fast; }
void screen_invalidate(void) { valid = 0; }

/* ------------------------------------------------------------------ a frame */
static void put_line(int y, const uint8_t *m) {
  uint8_t *p = lcd_line_addr(SCR_Y + y);
  if (fast) { for (int g = 0; g < CELLS; g++) p[gbyte[g]] = lut[m[g]]; return; }
  for (int x = 0; x < SCR_W; x++) {
    int black = m[x >> 3] >> (7 - (x & 7)) & 1;
    if (black == ink) p[xbyte[x]] |= xbit[x]; else p[xbyte[x]] &= (uint8_t)~xbit[x];
  }
}

int screen_render(Zx *x, int mode, int invert) {
  if (!last) return 0;
  const uint8_t *mem = x->mem;
  int flash = (x->frames >> 4) & 1, inv = invert ? 0xFF : 0x00;
  int any = 0;
  for (int y = 0; y < SCR_H; y++) {
    const uint8_t *pix = mem + (0x4000 | ((y & 0xC0) << 5) | ((y & 7) << 8) | ((y & 0x38) << 2));
    const uint8_t *att = mem + 0x5800 + (y >> 3) * CELLS;
    uint8_t m[CELLS];
    if (mode == MONO_INK)
      for (int c = 0; c < CELLS; c++) m[c] = (uint8_t)(pix[c] ^ inv);
    else
      for (int c = 0; c < CELLS; c++) {
        uint8_t a = att[c], an = cell_and[a], xo = cell_xor[a];
        if ((a & 0x80) && flash) xo ^= an;                       /* FLASH: ink and paper swap */
        m[c] = (uint8_t)(((pix[c] & an) ^ xo) ^ inv);
      }
    dirty[y] = 0;
    if (!valid || memcmp(m, last[y], CELLS)) {
      memcpy(last[y], m, CELLS);
      put_line(y, m);
      dirty[y] = 1;
      any++;
    }
  }
  valid = 1;
  if (!any) return 0;
  /* send the changed bands; bands closer than 6 lines go as one */
  for (int y = 0; y < SCR_H;) {
    if (!dirty[y]) { y++; continue; }
    int s = y, e = y + 1, gap = 0;
    for (int k = e; k < SCR_H && gap < 6; k++) { if (dirty[k]) { e = k + 1; gap = 0; } else gap++; }
    lcd_refresh_lines(SCR_Y + s, e - s);
    lcd_refresh_wait();
    y = e;
  }
  return any;
}

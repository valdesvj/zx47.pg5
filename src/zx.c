/* zx.c - ZX47: the 48K ZX Spectrum. See zx.h.
 *
 * Timing is by frame, not by cycle: the interrupt at the start of each frame (held for 32 T-states, as the ULA
 * does), then 69888 T-states of instructions. Memory contention and the floating bus are not emulated (most games
 * do not need them). A HALT waiting for the next interrupt skips straight to the frame end: most games spend much of
 * each frame there, which saves the calculator's battery.
 *
 * The tape: when the ROM's LD-BYTES (0x0556) is called with a .TAP inserted, the next block is copied straight into
 * memory and the routine returns, as most emulators do ("flash load"): LOAD "" takes no time. Turbo loaders and
 * copy-protected tapes (.TZX) are not handled.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <stdlib.h>
#include <string.h>
#include "zx.h"

/* ------------------------------------------------------------------ memory and ports */
static uint8_t rd(void *u, uint16_t a) { return ((Zx *)u)->mem[a]; }
static void wr(void *u, uint16_t a, uint8_t v) { if (a >= 0x4000) ((Zx *)u)->mem[a] = v; }   /* the ROM stays */

static uint8_t pin(z80 *z, uint16_t port) {
  Zx *x = (Zx *)z->userdata;
  if (!(port & 1)) {                                        /* the ULA: the half-rows whose address line is 0 */
    uint8_t k = 0;
    for (int r = 0; r < 8; r++) if (!(port & (0x100u << r))) k |= x->keys[r];
    return (uint8_t)(0xA0 | (x->ear ? 0x40 : 0) | (~k & 0x1F));   /* issue 3: bit 6 echoes the last EAR out */
  }
  if ((port & 0xFF) == 0x1F) return x->kempston;            /* the Kempston joystick */
  return 0xFF;                                              /* nothing there (no floating bus) */
}

static void pout(z80 *z, uint16_t port, uint8_t v) {
  Zx *x = (Zx *)z->userdata;
  if (port & 1) return;
  x->border = v & 7;
  uint8_t ear = (v >> 4) & 1;
  if (ear != x->ear) x->toggles_run++;
  x->ear = ear;
}

/* ------------------------------------------------------------------ the machine */
int zx_init(Zx *x) {
  memset(x, 0, sizeof *x);
  x->mem = malloc(65536);
  if (!x->mem) return -1;
  zx_reset(x);
  return 0;
}

void zx_reset(Zx *x) {
  z80_init(&x->cpu);
  x->cpu.read_byte = rd; x->cpu.write_byte = wr; x->cpu.port_in = pin; x->cpu.port_out = pout;
  x->cpu.userdata = x;
  memcpy(x->mem, zx_rom48, 16384);
  memset(x->mem + 16384, 0, 49152);
  x->t = 0;
  x->ear = 0;
  x->border = 7;
  x->tap_pos = 0;
}

void zx_key(Zx *x, int zk, int down) {
  if (zk == ZK_NONE) return;
  uint8_t m = (uint8_t)(1u << (zk & 7));
  if (down) x->keys[zk >> 3] |= m; else x->keys[zk >> 3] &= (uint8_t)~m;
}

void zx_keys_clear(Zx *x) { memset(x->keys, 0, sizeof x->keys); x->kempston = 0; }

/* ------------------------------------------------------------------ the tape: LD-BYTES */
static uint16_t pop16(z80 *z) {
  uint16_t v = (uint16_t)(z->read_byte(z->userdata, z->sp) | (z->read_byte(z->userdata, (uint16_t)(z->sp + 1)) << 8));
  z->sp = (uint16_t)(z->sp + 2);
  return v;
}

static int tape_trap(Zx *x) {
  z80 *z = &x->cpu;
  if (x->tap_pos + 2 > x->tap_len) return 0;                /* no more blocks: the ROM waits (BREAK stops it) */
  uint32_t blen = x->tap[x->tap_pos] | (uint32_t)x->tap[x->tap_pos + 1] << 8;
  const uint8_t *b = x->tap + x->tap_pos + 2;
  if (x->tap_pos + 2 + blen > x->tap_len) blen = x->tap_len - x->tap_pos - 2;   /* a cut file */
  x->tap_pos += 2 + blen;
  int ok = 0;
  uint16_t ix = z->ix, de = (uint16_t)((z->d << 8) | z->e);
  if (blen >= 1 && b[0] == z->a) {                          /* the flag the caller wants (header 0, data 0xFF) */
    uint8_t parity = b[0];
    uint32_t i = 0, have = blen - 1;                        /* the data, then the checksum */
    int load = z->cf, same = 1;                             /* carry set: LOAD, reset: VERIFY */
    for (; i < de && i < have; i++) {
      uint8_t v = b[1 + i];
      parity ^= v;
      if (load) wr(x, (uint16_t)(ix + i), v);
      else if (x->mem[(uint16_t)(ix + i)] != v) same = 0;
    }
    ok = i == de && have > de && (parity ^ b[1 + de]) == 0 && same;
    ix = (uint16_t)(ix + i);
    de = (uint16_t)(de - i);
    x->tap_blocks++;
  }
  z->ix = ix;
  z->d = (uint8_t)(de >> 8); z->e = (uint8_t)de;
  z->cf = (bool)ok;
  z->zf = 0;
  z->pc = pop16(z);                                         /* return from LD-BYTES */
  z->cyc += 1000;                                           /* some time passes */
  return 1;
}

void zx_tape(Zx *x, const uint8_t *tap, uint32_t n) { x->tap = tap; x->tap_len = tap ? n : 0; x->tap_pos = 0; x->tap_blocks = 0; }

/* ------------------------------------------------------------------ a frame */
void zx_frame(Zx *x) {
  z80 *z = &x->cpu;
  x->toggles_run = 0;
  unsigned long c0 = z->cyc - x->t;                         /* the cycle count at this frame's start */
  z80_gen_int(z, 0xFF);                                     /* IM 2 reads 0xFF from the bus */
  for (;;) {
    uint32_t t = (uint32_t)(z->cyc - c0);
    if (t >= ZX_T_FRAME) { x->t = t - ZX_T_FRAME; break; }
    if (z->int_pending && t >= 32) z->int_pending = 0;      /* the ULA holds INT for 32 T-states only */
    if (z->halted && !z->int_pending) {                     /* HALT: nothing happens until the next interrupt */
      uint32_t skip = (ZX_T_FRAME - t + 3) & ~3u;
      z->r = (uint8_t)((z->r & 0x80) | ((z->r + skip / 4) & 0x7F));
      z->cyc += skip;
      continue;
    }
    if (z->pc == 0x0556 && x->tap && tape_trap(x)) continue;
    z80_step(z);
  }
  x->toggles = x->toggles_run;
  x->frames++;
}

/* ------------------------------------------------------------------ snapshots */
static void set_regs_common(z80 *z) { z->halted = 0; z->int_pending = 0; z->nmi_pending = 0; z->iff_delay = 0; }

static void setf(z80 *z, uint8_t f) {
  z->sf = f >> 7 & 1; z->zf = f >> 6 & 1; z->yf = f >> 5 & 1; z->hf = f >> 4 & 1;
  z->xf = f >> 3 & 1; z->pf = f >> 2 & 1; z->nf = f >> 1 & 1; z->cf = f & 1;
}

static uint8_t getf(z80 *z) {
  return (uint8_t)(z->sf << 7 | z->zf << 6 | z->yf << 5 | z->hf << 4 | z->xf << 3 | z->pf << 2 | z->nf << 1 | z->cf);
}

/* .SNA (48K): 27 bytes of registers, then 0x4000..0xFFFF; PC is on the stack (the snapshot was taken in an NMI) */
int zx_load_sna(Zx *x, const uint8_t *d, uint32_t n) {
  if (n != ZX_SNA_SIZE) return n > ZX_SNA_SIZE ? ZXE_128K : ZXE_SIZE;
  zx_reset(x);
  z80 *z = &x->cpu;
  memcpy(x->mem + 0x4000, d + 27, 49152);
  z->i = d[0];
  z->l_ = d[1]; z->h_ = d[2]; z->e_ = d[3]; z->d_ = d[4]; z->c_ = d[5]; z->b_ = d[6]; z->f_ = d[7]; z->a_ = d[8];
  z->l = d[9]; z->h = d[10]; z->e = d[11]; z->d = d[12]; z->c = d[13]; z->b = d[14];
  z->iy = (uint16_t)(d[15] | d[16] << 8); z->ix = (uint16_t)(d[17] | d[18] << 8);
  z->iff1 = z->iff2 = (d[19] >> 2) & 1;
  z->r = d[20];
  setf(z, d[21]); z->a = d[22];
  z->sp = (uint16_t)(d[23] | d[24] << 8);
  z->interrupt_mode = d[25] & 3;
  x->border = d[26] & 7;
  set_regs_common(z);
  z->pc = pop16(z);                                         /* RETN */
  return 0;
}

int zx_save_sna(Zx *x, uint8_t *d) {
  z80 *z = &x->cpu;
  uint16_t sp = (uint16_t)(z->sp - 2);                      /* PC pushed, as an NMI would */
  wr(x, sp, (uint8_t)z->pc); wr(x, (uint16_t)(sp + 1), (uint8_t)(z->pc >> 8));
  d[0] = z->i;
  d[1] = z->l_; d[2] = z->h_; d[3] = z->e_; d[4] = z->d_; d[5] = z->c_; d[6] = z->b_; d[7] = z->f_; d[8] = z->a_;
  d[9] = z->l; d[10] = z->h; d[11] = z->e; d[12] = z->d; d[13] = z->c; d[14] = z->b;
  d[15] = (uint8_t)z->iy; d[16] = (uint8_t)(z->iy >> 8); d[17] = (uint8_t)z->ix; d[18] = (uint8_t)(z->ix >> 8);
  d[19] = (uint8_t)(z->iff2 ? 4 : 0);
  d[20] = z->r;
  d[21] = getf(z); d[22] = z->a;
  d[23] = (uint8_t)sp; d[24] = (uint8_t)(sp >> 8);
  d[25] = z->interrupt_mode;
  d[26] = x->border;
  memcpy(d + 27, x->mem + 0x4000, 49152);
  wr(x, sp, 0); wr(x, (uint16_t)(sp + 1), 0);               /* the stack as it was (those bytes were free) */
  return 0;
}

/* .Z80: version 1 (one 48K block), 2 and 3 (16K pages); the ED ED nn bb run-length packing */
static int unpack(const uint8_t *s, uint32_t n, uint8_t *o, uint32_t want, int v1) {
  uint32_t i = 0, k = 0;
  while (i < n && k < want) {
    if (v1 && i + 3 < n && s[i] == 0 && s[i + 1] == 0xED && s[i + 2] == 0xED && s[i + 3] == 0) break;   /* the end */
    if (i + 3 < n && s[i] == 0xED && s[i + 1] == 0xED) {
      for (int r = 0; r < s[i + 2] && k < want; r++) o[k++] = s[i + 3];
      i += 4;
    } else o[k++] = s[i++];
  }
  return k == want;
}

int zx_load_z80(Zx *x, const uint8_t *d, uint32_t n) {
  if (n < 30) return ZXE_SIZE;
  uint16_t pc = (uint16_t)(d[6] | d[7] << 8);
  uint32_t hdr = 30;
  int v1 = pc != 0;
  if (!v1) {
    if (n < 34) return ZXE_SIZE;
    uint32_t extra = d[30] | (uint32_t)d[31] << 8;
    hdr = 32 + extra;
    if (n < hdr) return ZXE_SIZE;
    pc = (uint16_t)(d[32] | d[33] << 8);
    uint8_t hw = d[34];
    int is48 = extra == 23 ? hw <= 1 : hw <= 1 || hw == 3;
    if (!is48) return ZXE_128K;
  }
  zx_reset(x);
  z80 *z = &x->cpu;
  uint8_t f12 = d[12] == 255 ? 1 : d[12];
  if (v1) {
    if (f12 & 0x20) { if (!unpack(d + 30, n - 30, x->mem + 0x4000, 49152, 1)) return ZXE_FORMAT; }
    else { if (n < 30 + 49152) return ZXE_SIZE; memcpy(x->mem + 0x4000, d + 30, 49152); }
  } else {
    int got = 0;
    for (uint32_t i = hdr; i + 3 <= n;) {
      uint32_t len = d[i] | (uint32_t)d[i + 1] << 8;
      uint8_t page = d[i + 2];
      i += 3;
      uint32_t raw = len == 0xFFFF;
      if (raw) len = 16384;
      if (i + len > n) return ZXE_SIZE;
      uint16_t at = page == 8 ? 0x4000 : page == 4 ? 0x8000 : page == 5 ? 0xC000 : 0;
      if (at) {
        if (raw) memcpy(x->mem + at, d + i, 16384);
        else if (!unpack(d + i, len, x->mem + at, 16384, 0)) return ZXE_FORMAT;
        got++;
      }
      i += len;
    }
    if (got < 3) return ZXE_FORMAT;
  }
  z->a = d[0]; setf(z, d[1]);
  z->c = d[2]; z->b = d[3]; z->l = d[4]; z->h = d[5];
  z->pc = pc;
  z->sp = (uint16_t)(d[8] | d[9] << 8);
  z->i = d[10];
  z->r = (uint8_t)((d[11] & 0x7F) | (f12 & 1) << 7);
  x->border = (f12 >> 1) & 7;
  z->e = d[13]; z->d = d[14];
  z->c_ = d[15]; z->b_ = d[16]; z->e_ = d[17]; z->d_ = d[18]; z->l_ = d[19]; z->h_ = d[20];
  z->a_ = d[21]; z->f_ = d[22];
  z->iy = (uint16_t)(d[23] | d[24] << 8); z->ix = (uint16_t)(d[25] | d[26] << 8);
  z->iff1 = d[27] ? 1 : 0; z->iff2 = d[28] ? 1 : 0;
  z->interrupt_mode = d[29] & 3;
  set_regs_common(z);
  return 0;
}

const char *zx_error(int e) {
  switch (e) {
  case ZXE_SIZE: return "wrong size";
  case ZXE_128K: return "128K: not yet (48K only)";
  case ZXE_FORMAT: return "bad file";
  default: return e ? "error" : "ok";
  }
}

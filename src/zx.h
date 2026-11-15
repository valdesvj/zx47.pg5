/* zx.h - ZX47: the 48K ZX Spectrum (Z80 at 3.5 MHz, 64 KB, the ULA's keyboard and beeper port, 50 Hz interrupt),
 * its tape (a .TAP file read by trapping the ROM's LD-BYTES) and snapshots (.SNA, .Z80).
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#ifndef ZX47_ZX_H
#define ZX47_ZX_H
#include <stdint.h>
#include "z80.h"

#define ZX_T_FRAME 69888u               /* T-states in a 48K frame (50.08 Hz) */

/* a Spectrum key: half-row (0..7, address line A8..A15) * 8 + bit (0..4) */
#define ZK(row, bit) ((row) * 8 + (bit))
enum {
  ZK_CAPS = ZK(0, 0), ZK_Z = ZK(0, 1), ZK_X = ZK(0, 2), ZK_C = ZK(0, 3), ZK_V = ZK(0, 4),
  ZK_A = ZK(1, 0), ZK_S = ZK(1, 1), ZK_D = ZK(1, 2), ZK_F = ZK(1, 3), ZK_G = ZK(1, 4),
  ZK_Q = ZK(2, 0), ZK_W = ZK(2, 1), ZK_E = ZK(2, 2), ZK_R = ZK(2, 3), ZK_T = ZK(2, 4),
  ZK_1 = ZK(3, 0), ZK_2 = ZK(3, 1), ZK_3 = ZK(3, 2), ZK_4 = ZK(3, 3), ZK_5 = ZK(3, 4),
  ZK_0 = ZK(4, 0), ZK_9 = ZK(4, 1), ZK_8 = ZK(4, 2), ZK_7 = ZK(4, 3), ZK_6 = ZK(4, 4),
  ZK_P = ZK(5, 0), ZK_O = ZK(5, 1), ZK_I = ZK(5, 2), ZK_U = ZK(5, 3), ZK_Y = ZK(5, 4),
  ZK_ENTER = ZK(6, 0), ZK_L = ZK(6, 1), ZK_K = ZK(6, 2), ZK_J = ZK(6, 3), ZK_H = ZK(6, 4),
  ZK_SPACE = ZK(7, 0), ZK_SYM = ZK(7, 1), ZK_M = ZK(7, 2), ZK_N = ZK(7, 3), ZK_B = ZK(7, 4),
  ZK_NONE = 0xFF
};

/* Kempston joystick bits (port 0x1F, 1 = pressed) */
#define KJ_RIGHT 0x01
#define KJ_LEFT  0x02
#define KJ_DOWN  0x04
#define KJ_UP    0x08
#define KJ_FIRE  0x10

typedef struct {
  z80 cpu;
  uint8_t *mem;                         /* 64 KB on the heap; the ROM copied into 0x0000..0x3FFF */
  uint32_t t;                           /* T-states into the frame */
  uint32_t frames;                      /* frames run (FLASH changes every 16) */
  uint8_t keys[8];                      /* the keys down, a bit per key (1 = down), by half-row */
  uint8_t kempston;                     /* KJ_* bits */
  uint8_t border, ear;                  /* the last OUT to 0xFE: border colour, the EAR/beeper bit */
  uint16_t toggles;                     /* beeper edges in the last frame (for the buzzer) */
  uint16_t toggles_run;
  const uint8_t *tap;                   /* the tape: a .TAP image (blocks), read by the LD-BYTES trap */
  uint32_t tap_len, tap_pos;
  uint16_t tap_blocks;                  /* blocks loaded */
} Zx;

extern const uint8_t zx_rom48[16384];   /* build/gen/rom48.c, from roms/48.rom */

int  zx_init(Zx *x);                    /* 0 ok, -1 no memory */
void zx_reset(Zx *x);
void zx_frame(Zx *x);                   /* one 50 Hz frame: the interrupt, then 69888 T-states */
void zx_key(Zx *x, int zk, int down);
void zx_keys_clear(Zx *x);

/* snapshots and tape: 0 ok, or a negative error (zx_error) */
int  zx_load_sna(Zx *x, const uint8_t *d, uint32_t n);
int  zx_load_z80(Zx *x, const uint8_t *d, uint32_t n);
int  zx_save_sna(Zx *x, uint8_t *d);    /* d: ZX_SNA_SIZE bytes */
void zx_tape(Zx *x, const uint8_t *tap, uint32_t n);   /* insert a .TAP (NULL: none); LOAD "" reads it */
const char *zx_error(int e);
#define ZX_SNA_SIZE (27 + 49152)
enum { ZXE_SIZE = -1, ZXE_128K = -2, ZXE_FORMAT = -3 };
#endif

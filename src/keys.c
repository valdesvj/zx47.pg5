/* keys.c - ZX47: the DM42n's keys on the Spectrum. The letters follow TERM47 (A..O on the top keys, P..Z on the
 * digit block), so a Spectrum keyword is the key with its letter (P = PRINT, J = LOAD ...).
 *
 *   all layers   SIGMA..E = A..O   ENTER = ENTER   R/S = SPACE   SHIFT = CAPS SHIFT   . = SYMBOL SHIFT
 *                <- = DELETE   up/down = cursor up/down   - = BREAK   0 = 0 (JOY: fire)
 *                F1 F2 = cursor left/right   F3 = EXTEND MODE   F4 = EDIT   F5 = GRAPHICS   F6 = CAPS LOCK
 *   ALPHA        7 8 9 / = P Q R S    4 5 6 x = T U V W    1 2 3 = X Y Z
 *   NUM          7 8 9 4 5 6 1 2 3 = digits,  / = /,  x = *
 *   JOY          the digit block is a joystick: 8 2 4 6 = up down left right, 7 9 1 3 diagonals, 5 0 = fire,
 *                up/down = up/down; as a Kempston joystick, cursor keys (5 6 7 8 0) or Sinclair (6 7 8 9 0)
 * + changes the layer and EXIT opens ZX47's menu (main.c); they are not Spectrum keys.
 * The calculator reads two keys at once: enough for CAPS or SYMBOL SHIFT + a key, or moving + fire.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <main.h>
#include <dmcp.h>
#include "keys.h"

const char *const LAYER_NAME[LAYER_CNT] = { "ALPHA", "NUM", "JOY" };
const char *const JOY_NAME[JOY_CNT] = { "Kempston", "Cursor", "Sinclair" };

typedef struct { uint8_t key, a1, a2, n1, n2; } Map;   /* calculator key -> Spectrum keys in ALPHA, in NUM */
#define N ZK_NONE
static const Map MAP[] = {
  {KEY_SIGMA, ZK_A, N, ZK_A, N}, {KEY_INV, ZK_B, N, ZK_B, N}, {KEY_SQRT, ZK_C, N, ZK_C, N},
  {KEY_LOG, ZK_D, N, ZK_D, N}, {KEY_LN, ZK_E, N, ZK_E, N}, {KEY_XEQ, ZK_F, N, ZK_F, N},
  {KEY_STO, ZK_G, N, ZK_G, N}, {KEY_RCL, ZK_H, N, ZK_H, N}, {KEY_RDN, ZK_I, N, ZK_I, N},
  {KEY_SIN, ZK_J, N, ZK_J, N}, {KEY_COS, ZK_K, N, ZK_K, N}, {KEY_TAN, ZK_L, N, ZK_L, N},
  {KEY_SWAP, ZK_M, N, ZK_M, N}, {KEY_CHS, ZK_N, N, ZK_N, N}, {KEY_E, ZK_O, N, ZK_O, N},
  {KEY_7, ZK_P, N, ZK_7, N}, {KEY_8, ZK_Q, N, ZK_8, N}, {KEY_9, ZK_R, N, ZK_9, N}, {KEY_DIV, ZK_S, N, ZK_SYM, ZK_V},
  {KEY_4, ZK_T, N, ZK_4, N}, {KEY_5, ZK_U, N, ZK_5, N}, {KEY_6, ZK_V, N, ZK_6, N}, {KEY_MUL, ZK_W, N, ZK_SYM, ZK_B},
  {KEY_1, ZK_X, N, ZK_1, N}, {KEY_2, ZK_Y, N, ZK_2, N}, {KEY_3, ZK_Z, N, ZK_3, N},
  {KEY_0, ZK_0, N, ZK_0, N},
  {KEY_ENTER, ZK_ENTER, N, ZK_ENTER, N}, {KEY_RUN, ZK_SPACE, N, ZK_SPACE, N},
  {KEY_SHIFT, ZK_CAPS, N, ZK_CAPS, N}, {KEY_DOT, ZK_SYM, N, ZK_SYM, N},
  {KEY_BSP, ZK_CAPS, ZK_0, ZK_CAPS, ZK_0}, {KEY_UP, ZK_CAPS, ZK_7, ZK_CAPS, ZK_7},
  {KEY_DOWN, ZK_CAPS, ZK_6, ZK_CAPS, ZK_6}, {KEY_SUB, ZK_CAPS, ZK_SPACE, ZK_CAPS, ZK_SPACE},
  {KEY_F1, ZK_CAPS, ZK_5, ZK_CAPS, ZK_5}, {KEY_F2, ZK_CAPS, ZK_8, ZK_CAPS, ZK_8},
  {KEY_F3, ZK_CAPS, ZK_SYM, ZK_CAPS, ZK_SYM}, {KEY_F4, ZK_CAPS, ZK_1, ZK_CAPS, ZK_1},
  {KEY_F5, ZK_CAPS, ZK_9, ZK_CAPS, ZK_9}, {KEY_F6, ZK_CAPS, ZK_2, ZK_CAPS, ZK_2},
};
#undef N

/* JOY: the digit block (and up/down) as joystick directions */
static uint8_t joy_dir(int k) {
  switch (k) {
  case KEY_8: case KEY_UP: return KJ_UP;
  case KEY_2: case KEY_DOWN: return KJ_DOWN;
  case KEY_4: return KJ_LEFT;
  case KEY_6: return KJ_RIGHT;
  case KEY_7: return KJ_UP | KJ_LEFT;
  case KEY_9: return KJ_UP | KJ_RIGHT;
  case KEY_1: return KJ_DOWN | KJ_LEFT;
  case KEY_3: return KJ_DOWN | KJ_RIGHT;
  case KEY_5: case KEY_0: return KJ_FIRE;
  default: return 0;
  }
}

static void joystick(Zx *x, uint8_t j, int type) {
  if (type == JOY_KEMPSTON) { x->kempston |= j; return; }
  /* cursor: 5 6 7 8 = left down up right, 0 fire; Sinclair (Interface 2, port 1): 6 7 8 9 = left right down up */
  static const uint8_t CUR[5] = { ZK_8, ZK_5, ZK_6, ZK_7, ZK_0 };     /* right left down up fire */
  static const uint8_t SIN[5] = { ZK_7, ZK_6, ZK_8, ZK_9, ZK_0 };
  const uint8_t *t = type == JOY_CURSOR ? CUR : SIN;
  for (int b = 0; b < 5; b++) if (j & (1u << b)) zx_key(x, t[b], 1);
}

static void one(Zx *x, int k, int layer, int joy) {
  if (k <= 0) return;
  if (layer == LAYER_JOY) {
    uint8_t j = joy_dir(k);
    if (j) { joystick(x, j, joy); return; }
  }
  for (unsigned i = 0; i < sizeof MAP / sizeof *MAP; i++)
    if (MAP[i].key == k) {
      int num = layer == LAYER_NUM;
      zx_key(x, num ? MAP[i].n1 : MAP[i].a1, 1);
      zx_key(x, num ? MAP[i].n2 : MAP[i].a2, 1);
      return;
    }
}

void keys_apply(Zx *x, int k1, int k2, int layer, int joy) {
  zx_keys_clear(x);
  one(x, k1, layer, joy);
  if (k2 != k1) one(x, k2, layer, joy);
}

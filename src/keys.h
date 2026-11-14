/* keys.h - ZX47: the DM42n's keys as the Spectrum's keyboard and joystick.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#ifndef ZX47_KEYS_H
#define ZX47_KEYS_H
#include "zx.h"

enum { LAYER_ALPHA, LAYER_NUM, LAYER_JOY, LAYER_CNT };
enum { JOY_KEMPSTON, JOY_CURSOR, JOY_SINCLAIR, JOY_CNT };

extern const char *const LAYER_NAME[LAYER_CNT];
extern const char *const JOY_NAME[JOY_CNT];

/* the keys held now (k1, k2: DMCP key codes, 0 = none) as Spectrum keys and joystick */
void keys_apply(Zx *x, int k1, int k2, int layer, int joy);
#endif

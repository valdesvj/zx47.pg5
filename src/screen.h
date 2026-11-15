/* screen.h - ZX47: the Spectrum's 256x192 screen on the DM42n's LCD, 1:1 at (72, 24), only the changed lines sent.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#ifndef ZX47_SCREEN_H
#define ZX47_SCREEN_H
#include <stdint.h>
#include "zx.h"

#define SCR_X 72                        /* the picture on the LCD (byte-aligned) */
#define SCR_Y 24
#define SCR_W 256
#define SCR_H 192

enum { MONO_COLOUR, MONO_INK };         /* colour: the darker of ink and paper is black; ink: set pixels black */

int  screen_init(void);                 /* learns the LCD buffer's layout; 0 ok, -1 no memory */
void screen_invalidate(void);           /* the next render sends every line (after a menu) */
int  screen_render(Zx *x, int mode, int invert);   /* the changed lines to the LCD; returns lines sent */
int  screen_fast(void);                 /* 1 if the byte-wise path is used */
#endif

#!/usr/bin/env python3
"""keymap.py OUT.png - ZX47's keys drawn on the DM42n's keyboard, for docs/MANUAL.md.
Each key: the calculator's legend (small, top), the Spectrum key in ALPHA (big), NUM and JOY when they differ
(small, bottom). Keep it in step with src/keys.c."""
import sys
from PIL import Image, ImageDraw, ImageFont

F = '/usr/share/fonts/truetype/dejavu/'
small = ImageFont.truetype(F + 'DejaVuSansCondensed.ttf', 15)
big = ImageFont.truetype(F + 'DejaVuSansCondensed-Bold.ttf', 22)
title = ImageFont.truetype(F + 'DejaVuSans-Bold.ttf', 24)

# (calculator legend, ALPHA, NUM, JOY, width in columns); None: as ALPHA
ROWS = [
    [('F1', 'cur ←', None, None, 1), ('F2', 'cur →', None, None, 1), ('F3', 'EXTEND', None, None, 1),
     ('F4', 'EDIT', None, None, 1), ('F5', 'GRAPH', None, None, 1), ('F6', 'CAPS LK', None, None, 1)],
    [('Σ+', 'A', None, None, 1), ('1/x', 'B', None, None, 1), ('√x', 'C', None, None, 1),
     ('LOG', 'D', None, None, 1), ('LN', 'E', None, None, 1), ('XEQ', 'F', None, None, 1)],
    [('STO', 'G', None, None, 1), ('RCL', 'H', None, None, 1), ('R↓', 'I', None, None, 1),
     ('SIN', 'J', None, None, 1), ('COS', 'K', None, None, 1), ('TAN', 'L', None, None, 1)],
    [('ENTER', 'ENTER', None, None, 2), ('x≷y', 'M', None, None, 1), ('+/−', 'N', None, None, 1),
     ('E', 'O', None, None, 1), ('←', 'DELETE', None, None, 1)],
    [('▲', 'cur ↑', None, 'up', 1), ('7', 'P', '7', '↖', 1), ('8', 'Q', '8', 'up', 1), ('9', 'R', '9', '↗', 1),
     ('÷', 'S', '/', None, 1)],
    [('▼', 'cur ↓', None, 'down', 1), ('4', 'T', '4', 'left', 1), ('5', 'U', '5', 'FIRE', 1),
     ('6', 'V', '6', 'right', 1), ('×', 'W', '*', None, 1)],
    [('SHIFT', 'CAPS', None, None, 1), ('1', 'X', '1', '↙', 1), ('2', 'Y', '2', 'down', 1), ('3', 'Z', '3', '↘', 1),
     ('−', 'BREAK', None, None, 1)],
    [('EXIT', 'MENU', None, None, 1), ('0', '0', None, 'FIRE', 1), ('.', 'SYMBOL', None, None, 1),
     ('R/S', 'SPACE', None, None, 1), ('+', 'LAYER', None, None, 1)],
]

W, KH, GAP, M = 1000, 92, 10, 24
TOP = 70
im = Image.new('RGB', (W, TOP + len(ROWS) * (KH + GAP) + 110), 'white')
d = ImageDraw.Draw(im)
d.text((M, 18), 'ZX47 keys on the DM42n', font=title, fill='black')
d.text((M + 360, 24), 'big: ALPHA layer   ·   bottom: NUM / JOY when different   ·   + changes the layer',
       font=small, fill='#444')

for r, row in enumerate(ROWS):
    units = sum(k[4] for k in row)                       # ENTER is two keys wide
    unit = (W - 2 * M - (units - 1) * GAP) / units
    x = M
    y = TOP + r * (KH + GAP)
    for leg, a, n, j, w in row:
        kw = unit * w + GAP * (w - 1)
        special = a in ('MENU', 'LAYER')
        d.rounded_rectangle([x, y, x + kw, y + KH], radius=10, fill='#20262a' if special else '#e9ece6',
                            outline='#20262a', width=2)
        fg = 'white' if special else 'black'
        d.text((x + 8, y + 5), leg, font=small, fill='#bbb' if special else '#555')
        tw = d.textlength(a, font=big)
        d.text((x + (kw - tw) / 2, y + 30), a, font=big, fill=fg)
        extra = []
        if n and n != a:
            extra.append('NUM ' + n)
        if j:
            extra.append('JOY ' + j)
        if extra:
            s = '  '.join(extra)
            d.text((x + kw - d.textlength(s, font=small) - 8, y + KH - 22), s, font=small, fill='#1a5fb4')
        x += kw + GAP

y = TOP + len(ROWS) * (KH + GAP) + 6
for i, line in enumerate([
        'SHIFT (CAPS SHIFT) and . (SYMBOL SHIFT): hold them with the other key: . + P = "   . + K = +   SHIFT + ← = DELETE',
        'A Spectrum keyword is the key with its letter: P = PRINT, J = LOAD, R = RUN.   SHIFT + + = screenshot.',
        'JOY: Kempston, cursor or Sinclair joystick (menu 4).   EXIT: the menu (load, save, setup, off).']):
    d.text((M, y + i * 24), line, font=small, fill='black')
im.save(sys.argv[1])

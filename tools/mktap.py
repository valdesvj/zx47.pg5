#!/usr/bin/env python3
"""mktap.py SOURCE.bas OUT.tap [NAME] - a Spectrum BASIC listing as a .TAP that LOAD "" loads and runs.

The listing: one line per BASIC line, "10 PRINT ..."; keywords in CAPITALS (GO TO, GO SUB and DEF FN with their
space), variables in small letters. Numbers get their hidden 5-byte form, as the ROM writes them. The first line
number is the autostart line. Only for small listings of your own (ZX47's demo and tests)."""
import sys

KEYWORDS = {
    'RND': 0xA5, 'INKEY$': 0xA6, 'PI': 0xA7, 'FN': 0xA8, 'POINT': 0xA9, 'SCREEN$': 0xAA, 'ATTR': 0xAB, 'AT': 0xAC,
    'TAB': 0xAD, 'VAL$': 0xAE, 'CODE': 0xAF, 'VAL': 0xB0, 'LEN': 0xB1, 'SIN': 0xB2, 'COS': 0xB3, 'TAN': 0xB4,
    'ASN': 0xB5, 'ACS': 0xB6, 'ATN': 0xB7, 'LN': 0xB8, 'EXP': 0xB9, 'INT': 0xBA, 'SQR': 0xBB, 'SGN': 0xBC,
    'ABS': 0xBD, 'PEEK': 0xBE, 'IN': 0xBF, 'USR': 0xC0, 'STR$': 0xC1, 'CHR$': 0xC2, 'NOT': 0xC3, 'BIN': 0xC4,
    'OR': 0xC5, 'AND': 0xC6, '<=': 0xC7, '>=': 0xC8, '<>': 0xC9, 'LINE': 0xCA, 'THEN': 0xCB, 'TO': 0xCC,
    'STEP': 0xCD, 'DEF FN': 0xCE, 'CAT': 0xCF, 'FORMAT': 0xD0, 'MOVE': 0xD1, 'ERASE': 0xD2, 'OPEN #': 0xD3,
    'CLOSE #': 0xD4, 'MERGE': 0xD5, 'VERIFY': 0xD6, 'BEEP': 0xD7, 'CIRCLE': 0xD8, 'INK': 0xD9, 'PAPER': 0xDA,
    'FLASH': 0xDB, 'BRIGHT': 0xDC, 'INVERSE': 0xDD, 'OVER': 0xDE, 'OUT': 0xDF, 'LPRINT': 0xE0, 'LLIST': 0xE1,
    'STOP': 0xE2, 'READ': 0xE3, 'DATA': 0xE4, 'RESTORE': 0xE5, 'NEW': 0xE6, 'BORDER': 0xE7, 'CONTINUE': 0xE8,
    'DIM': 0xE9, 'REM': 0xEA, 'FOR': 0xEB, 'GO TO': 0xEC, 'GO SUB': 0xED, 'INPUT': 0xEE, 'LOAD': 0xEF,
    'LIST': 0xF0, 'LET': 0xF1, 'PAUSE': 0xF2, 'NEXT': 0xF3, 'POKE': 0xF4, 'PRINT': 0xF5, 'PLOT': 0xF6,
    'RUN': 0xF7, 'SAVE': 0xF8, 'RANDOMIZE': 0xF9, 'IF': 0xFA, 'CLS': 0xFB, 'DRAW': 0xFC, 'CLEAR': 0xFD,
    'RETURN': 0xFE, 'COPY': 0xFF,
}
BY_LEN = sorted(KEYWORDS, key=len, reverse=True)


def number(text):
    """the hidden form: 0x0E and five bytes (small integers, or the floating form)"""
    v = float(text)
    if v == int(v) and 0 <= v <= 65535:
        i = int(v)
        return bytes([0x0E, 0, 0, i & 0xFF, i >> 8, 0])
    import math
    m, e = math.frexp(v)                       # v = m * 2**e, 0.5 <= m < 1
    mant = int(round(m * 2 ** 32)) & 0xFFFFFFFF
    mant &= 0x7FFFFFFF                         # the top bit (always 1) is the sign: 0 for positive
    return bytes([0x0E, e + 128]) + mant.to_bytes(4, 'big')


def tokenize(body):
    out, i = bytearray(), 0
    in_str = rem = False
    while i < len(body):
        c = body[i]
        if rem:
            out += c.encode('latin-1'); i += 1; continue
        if c == '"':
            in_str = not in_str; out += b'"'; i += 1; continue
        if in_str:
            out += c.encode('latin-1'); i += 1; continue
        prev = body[i - 1] if i else ' '
        for k in BY_LEN:
            if body.startswith(k, i) and (k[0] in '<>' or not prev.isalpha()):
                after = body[i + len(k):i + len(k) + 1]
                if k[-1].isalpha() and after.isalpha():
                    continue
                out.append(KEYWORDS[k]); i += len(k)
                if k == 'REM':
                    rem = True
                while i < len(body) and body[i] == ' ':     # the ROM prints the spaces itself
                    i += 1
                break
        else:
            if (c.isdigit() or (c == '.' and i + 1 < len(body) and body[i + 1].isdigit())) and not prev.isalnum():
                j = i
                while j < len(body) and (body[j].isdigit() or body[j] == '.'):
                    j += 1
                out += body[i:j].encode() + number(body[i:j]); i = j
            else:
                out += c.encode('latin-1'); i += 1
    return bytes(out)


def program(lines):
    prog, first = bytearray(), None
    for ln in lines:
        ln = ln.rstrip('\n')
        if not ln.strip():
            continue
        num, body = ln.split(' ', 1)
        n = int(num)
        first = n if first is None else first
        text = tokenize(body) + b'\x0d'
        prog += bytes([n >> 8, n & 0xFF]) + len(text).to_bytes(2, 'little') + text
    return bytes(prog), first


def block(flag, data):
    b = bytes([flag]) + data
    x = 0
    for v in b:
        x ^= v
    b += bytes([x])
    return len(b).to_bytes(2, 'little') + b


def main():
    src, out = sys.argv[1], sys.argv[2]
    name = (sys.argv[3] if len(sys.argv) > 3 else 'ZX47').ljust(10)[:10]
    prog, first = program(open(src).readlines())
    hdr = bytes([0]) + name.encode() + len(prog).to_bytes(2, 'little') + first.to_bytes(2, 'little') + \
        len(prog).to_bytes(2, 'little')
    open(out, 'wb').write(block(0x00, hdr) + block(0xFF, prog))


if __name__ == '__main__':
    main()

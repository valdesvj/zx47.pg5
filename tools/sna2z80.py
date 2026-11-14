#!/usr/bin/env python3
"""sna2z80.py IN.sna OUT.z80 [1|3] - a 48K .SNA as a .Z80 snapshot: version 1 (one packed block) or 3 (packed
16K pages). For ZX47's tests (the .Z80 loader against a known memory), and handy for your own snapshots."""
import sys


def pack(d):
    out, i, n = bytearray(), 0, len(d)
    while i < n:
        b, run = d[i], 1
        while i + run < n and d[i + run] == b and run < 255:
            run += 1
        if run >= 5 or (b == 0xED and run >= 2):
            out += bytes([0xED, 0xED, run, b]); i += run
        else:
            out.append(b); i += 1
            if b == 0xED and i < n:                  # a lone ED: the next byte may not start a block
                out.append(d[i]); i += 1
    return bytes(out)


def main():
    s = open(sys.argv[1], 'rb').read()
    ver = int(sys.argv[3]) if len(sys.argv) > 3 else 3
    assert len(s) == 27 + 49152, 'a 48K .SNA'
    mem = bytearray(65536)
    mem[0x4000:] = s[27:]
    sp = s[23] | s[24] << 8
    pc = mem[sp] | mem[sp + 1] << 8                  # the SNA keeps PC on the stack
    mem[sp] = mem[sp + 1] = 0
    sp = (sp + 2) & 0xFFFF
    r = s[20]
    h = bytearray(30)
    h[0], h[1] = s[22], s[21]                        # A F
    h[2], h[3], h[4], h[5] = s[13], s[14], s[9], s[10]   # C B L H
    h[8], h[9] = sp & 0xFF, sp >> 8
    h[10], h[11] = s[0], r & 0x7F
    h[12] = (r >> 7) | (s[26] & 7) << 1 | (0x20 if ver == 1 else 0)
    h[13], h[14] = s[11], s[12]                      # E D
    h[15:21] = bytes([s[5], s[6], s[3], s[4], s[1], s[2]])   # C' B' E' D' L' H'
    h[21], h[22] = s[8], s[7]                        # A' F'
    h[23:27] = bytes([s[15], s[16], s[17], s[18]])   # IY IX
    h[27] = h[28] = 1 if s[19] & 4 else 0
    h[29] = s[25] & 3
    if ver == 1:
        h[6], h[7] = pc & 0xFF, pc >> 8
        data = bytes(h) + pack(bytes(mem[0x4000:])) + b'\x00\xed\xed\x00'
    else:
        extra = bytearray(54)
        extra[0], extra[1] = pc & 0xFF, pc >> 8
        extra[2] = 0                                 # hardware: 48K
        data = bytes(h) + (54).to_bytes(2, 'little') + bytes(extra)
        for page, at in ((8, 0x4000), (4, 0x8000), (5, 0xC000)):
            p = pack(bytes(mem[at:at + 16384]))
            if len(p) >= 16384:
                data += (0xFFFF).to_bytes(2, 'little') + bytes([page]) + bytes(mem[at:at + 16384])
            else:
                data += len(p).to_bytes(2, 'little') + bytes([page]) + p
    open(sys.argv[2], 'wb').write(data)


if __name__ == '__main__':
    main()

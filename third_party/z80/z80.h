#ifndef Z80_Z80_H_
#define Z80_Z80_H_

#include <stdint.h>
#include <stdbool.h>

typedef struct z80 z80;
struct z80 {
  uint8_t (*read_byte)(void*, uint16_t);
  void (*write_byte)(void*, uint16_t, uint8_t);
  uint8_t (*port_in)(z80*, uint16_t);         /* ZX47: the full 16-bit port (the Spectrum's keyboard) */
  void (*port_out)(z80*, uint16_t, uint8_t);
  void* userdata;

  /* ZX47: memory is read and written straight from mem (no call per byte); writes below wlimit are ignored
     (the ROM); a write below wwatch sets watched (the Spectrum's screen). read_byte/write_byte are not used. */
  uint8_t* mem;
  uint16_t wlimit, wwatch;
  uint8_t watched;

  unsigned long cyc; // cycle count (t-states)

  uint16_t pc, sp, ix, iy; // special purpose registers
  uint16_t mem_ptr; // "wz" register
  uint8_t a, b, c, d, e, h, l; // main registers
  uint8_t a_, b_, c_, d_, e_, h_, l_, f_; // alternate registers
  uint8_t i, r; // interrupt vector, memory refresh

  // flags: sign, zero, yf, half-carry, xf, parity/overflow, negative, carry
  // ZX47: whole bytes, not bit-fields (a bit-field write is a read-modify-write on ARM)
  bool sf, zf, yf, hf, xf, pf, nf, cf;

  uint8_t iff_delay;
  uint8_t interrupt_mode;
  uint8_t int_data;
  bool iff1, iff2;
  bool halted;
  bool int_pending, nmi_pending;
};

void z80_init(z80* const z);
void z80_step(z80* const z);
void z80_gen_nmi(z80* const z);
void z80_gen_int(z80* const z, uint8_t data);

#endif // Z80_Z80_H_

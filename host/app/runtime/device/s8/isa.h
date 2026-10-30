#ifndef S8_FIXED_ISA_H
#define S8_FIXED_ISA_H
#include <stdint.h>
#if defined(__riscv) && (__riscv_xlen==32)
/* Implicit fixed floating-register operands must be visible to the compiler.
 * Clobber the complete floating bank: never retain a live value across a TCU
 * operation based on a guessed narrower writeback footprint. */
#define S8_FREG_CLOBBERS "f0","f1","f2","f3","f4","f5","f6","f7","f8","f9","f10","f11","f12","f13","f14","f15","f16","f17","f18","f19","f20","f21","f22","f23","f24","f25","f26","f27","f28","f29","f30","f31","memory"
static inline uint32_t s8_isa_begin(uint32_t control){uint32_t r;__asm__ volatile("fmv.w.x f10, %1\n\t.insn r 0x0b, 0, 2, x8, x13, x0\n\tfmv.x.w %0, f10":"=r"(r):"r"(control):S8_FREG_CLOBBERS);return r;}
static inline void s8_isa_step(uint32_t a,uint32_t b){__asm__ volatile("fmv.w.x f0, %0\n\tfmv.w.x f28, %1\n\t.insn r 0x0b, 0, 2, x8, x14, x0"::"r"(a),"r"(b):S8_FREG_CLOBBERS);}
static inline uint32_t s8_isa_read(uint32_t control){uint32_t r;__asm__ volatile("fmv.w.x f10, %1\n\t.insn r 0x0b, 0, 2, x8, x15, x0\n\tfmv.x.w %0, f10":"=r"(r):"r"(control):S8_FREG_CLOBBERS);return r;}
#endif
#endif

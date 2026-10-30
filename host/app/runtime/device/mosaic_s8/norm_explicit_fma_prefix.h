#ifndef MOSAIC_NORM_EXPLICIT_FMA_PREFIX_H
#define MOSAIC_NORM_EXPLICIT_FMA_PREFIX_H
// Optional per-translation-unit build variant, only for instance_norm_compat_v*.
// Never pass this forced include to default IN, convolution, or other operators.
#ifndef MOSAIC_NORM_COMPAT_EXPLICIT_ISA_FMA
#error "This header requires the independent IN FMA variant build scope"
#endif
#include <math.h>
static inline float mosaic_in_explicit_fma(float a,float b,float c){
#if defined(__riscv) && __riscv_xlen==32
 float value;
 __asm__ volatile("fmadd.s %0, %1, %2, %3, rne":"=f"(value):"f"(a),"f"(b),"f"(c));
 return value;
#else
 return fmaf(a,b,c);
#endif
}
// math.h has already been parsed; only calls in the selected source are mapped.
#define fmaf mosaic_in_explicit_fma
#endif

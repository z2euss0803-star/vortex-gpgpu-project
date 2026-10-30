#ifndef S8_REFERENCE_H
#define S8_REFERENCE_H
#include <stddef.h>
#include <stdint.h>
int s8_reference(const int16_t *a,const int16_t *b,uint32_t k,int64_t out[64]);
int s8_unpack48(const uint8_t raw[384],int64_t out[64]);
int s8_unpack48_checked(const uint8_t *raw,size_t bytes,int64_t out[64]);
int s8_compat_inputs(uint32_t,uint8_t *,uint32_t,uint32_t *,uint8_t *,uint32_t,uint32_t *);
int s8_compare_compat(uint32_t,const uint8_t *,uint32_t);
#endif

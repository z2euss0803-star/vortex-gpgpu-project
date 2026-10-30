#ifndef VXL_MOSAIC_NUMERIC_H
#define VXL_MOSAIC_NUMERIC_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int mosaic_quantize_rne(const float *,size_t,float,int16_t *);
int mosaic_dot_i64(const int16_t *,const int16_t *,size_t,int64_t *);
int mosaic_restore_f32(int64_t,float,float,float,float *);
#ifdef __cplusplus
}
#endif
#endif

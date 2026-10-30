#ifndef S8_DEVICE_COMPAT_H
#define S8_DEVICE_COMPAT_H
#include <stdint.h>
#include <vxl/s8_runtime.h>
struct s8_compat_io {
 void *context;
 int (*wmma)(void *,const uint16_t *,const uint16_t *,volatile float *);
 int (*texture)(void *,const uint32_t *,unsigned,volatile uint32_t *);
 void (*fence)(void *);
 uint32_t participant;
};
unsigned s8_compat_bytes(uint32_t);
int s8_execute_compat(const vxl_s8_job_v1 *,uint32_t,uint32_t,const uint8_t *,uint32_t,const uint8_t *,uint32_t,uint8_t *,uint32_t,const s8_compat_io *);
int s8_run_compat(const vxl_s8_job_v1 *,uint32_t,uint32_t);
#endif

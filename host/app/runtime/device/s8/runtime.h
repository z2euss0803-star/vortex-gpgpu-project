#ifndef S8_DEVICE_RUNTIME_H
#define S8_DEVICE_RUNTIME_H
#include <stdint.h>
#include <vx_spawn.h>
#include <vxl/s8_runtime.h>
extern __thread volatile uint32_t s8_tls_probe;
extern __thread const vxl_s8_job_v1 *s8_current_job;
static inline void s8_fence(){__asm__ volatile("fence iorw, iorw":::"memory");}
static inline void s8_barrier_group(){s8_fence();__syncthreads();s8_fence();}
static inline uint32_t s8_sp(){uint32_t v;__asm__ volatile("mv %0, sp":"=r"(v));return v;}
static inline uint32_t s8_tp(){uint32_t v;__asm__ volatile("mv %0, tp":"=r"(v));return v;}
void s8_callback(void *);
void s8_group_finish(uint32_t error);
/* Implemented by the controlled case modules; no success placeholder. */
int s8_run_job(const vxl_s8_job_v1 *,uint32_t core,uint32_t warp,uint32_t lane);
#endif

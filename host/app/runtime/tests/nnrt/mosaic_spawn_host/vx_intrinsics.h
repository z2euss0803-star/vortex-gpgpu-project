#ifndef VXM_HOST_SPAWN_INTRINSICS_H
#define VXM_HOST_SPAWN_INTRINSICS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define VX_CSR_MSCRATCH 0x340
unsigned vx_num_threads(void);
unsigned vx_num_warps(void);
unsigned vx_num_cores(void);
unsigned vx_core_id(void);
unsigned vx_warp_id(void);
unsigned vx_thread_id(void);
unsigned vx_hart_id(void);
uintptr_t vxm_host_csr_read(unsigned);
void vxm_host_csr_write(unsigned,uintptr_t);
#define csr_read(c) vxm_host_csr_read(c)
#define csr_write(c,v) vxm_host_csr_write(c,(uintptr_t)(v))
void vx_wspawn(unsigned,void (*)(void));
void vx_tmc(unsigned);
void vx_tmc_zero(void);
void vx_tmc_one(void);
#ifdef __cplusplus
}
#endif
#endif

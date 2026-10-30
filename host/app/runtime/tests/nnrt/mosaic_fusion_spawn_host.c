#include <vx_spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned core_id,warp_id,lane_id;
static uintptr_t scratch;
int fusion_host_launch(void *);
unsigned vx_num_threads(void){return 4;}
unsigned vx_num_warps(void){return 4;}
unsigned vx_num_cores(void){return 4;}
unsigned vx_core_id(void){return core_id;}
unsigned vx_warp_id(void){return warp_id;}
unsigned vx_thread_id(void){return lane_id;}
unsigned vx_hart_id(void){return core_id*16+warp_id*4+lane_id;}
uintptr_t vxm_host_csr_read(unsigned csr){if(csr!=VX_CSR_MSCRATCH)abort();return scratch;}
void vxm_host_csr_write(unsigned csr,uintptr_t v){if(csr!=VX_CSR_MSCRATCH)abort();scratch=v;}
void vx_tmc(unsigned mask){(void)mask;}
void vx_tmc_zero(void){}
void vx_tmc_one(void){}
void vx_wspawn(unsigned count,void (*entry)(void)){
 unsigned saved=warp_id;if(entry)for(warp_id=1;warp_id<count;++warp_id)entry();warp_id=saved;
}
int vx_printf(const char *fmt,...){va_list ap;va_start(ap,fmt);int rc=vfprintf(stderr,fmt,ap);va_end(ap);return rc;}
int fusion_host_dispatch(void *arg){
 for(core_id=0;core_id<4;++core_id)for(lane_id=0;lane_id<4;++lane_id){warp_id=0;scratch=0;int rc=fusion_host_launch(arg);if(rc)return rc;}
 return 0;
}

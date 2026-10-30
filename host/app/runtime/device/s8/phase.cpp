#include "phase.h"
#include <errno.h>
int s8_read_mcycle_from(const s8_cycle_io *io,uint64_t *raw){
 if(!io||!raw||!io->high||!io->low)return -EINVAL;
 for(unsigned attempt=0;attempt<4;++attempt){uint32_t a=io->high(io->context),lo=io->low(io->context),b=io->high(io->context);if(a==b){if(a&~4095u)return -ERANGE;*raw=(uint64_t(a)<<32)|lo;return 0;}}
 return -EAGAIN;
}
int s8_phase_init(volatile vxl_s8_phase_v1 *p,uint32_t core,uint32_t warp){
 if(!p||core>3||warp>3)return -EINVAL;
 auto *bytes=reinterpret_cast<volatile uint8_t *>(p);for(unsigned i=0;i<512;++i)bytes[i]=0;
 p->magic=S8_PHASE_MAGIC;p->version=1;p->bytes=512;p->core=core;p->warp=warp;p->width=44;return 0;
}
int s8_phase_store(volatile vxl_s8_phase_v1 *p,uint32_t event,uint64_t raw){
 if(!p||p->magic!=S8_PHASE_MAGIC||p->width!=44||p->count!=event||event>=12||raw>=(1ull<<44))return -EINVAL;
 p->events[event].event=event;p->events[event].raw_lo=uint32_t(raw);p->events[event].raw_hi=uint32_t(raw>>32);p->events[event].flags=1;p->valid_mask|=1u<<event;p->count=event+1;return 0;
}
#if defined(__riscv)
#include "runtime.h"
static uint32_t high(void *){uint32_t v;__asm__ volatile("csrr %0, 0xb80":"=r"(v));return v;}
static uint32_t low(void *){uint32_t v;__asm__ volatile("csrr %0, 0xb00":"=r"(v));return v;}
int s8_read_mcycle(uint64_t *raw){s8_cycle_io io={nullptr,high,low};return s8_read_mcycle_from(&io,raw);}
static volatile vxl_s8_phase_v1 *address(){const auto *j=s8_current_job;return reinterpret_cast<volatile vxl_s8_phase_v1 *>(uintptr_t(j->output_base+(vx_core_id()*4+j->owner_warp)*4096+0x6c0));}
int s8_phase_event(uint32_t event,uint64_t raw){return s8_phase_store(address(),event,raw);}
int s8_phase_mark(uint32_t event){
 const auto *j=s8_current_job;if(!j->flags||!(j->core_mask&(1u<<vx_core_id()))||vx_warp_id()!=j->owner_warp)return 0;
 int rc=0;if(vx_thread_id()==0){uint64_t raw=0;rc=s8_read_mcycle(&raw);if(!rc)rc=s8_phase_event(event,raw);}s8_fence();return int(vx_shfl_idx(uint32_t(rc),0,3,0));
}
int s8_phase_open(const vxl_s8_job_v1 *j,uint32_t core,uint32_t warp){
 if(!j->flags||!(j->core_mask&(1u<<core))||warp!=j->owner_warp)return 0;
 if(vx_thread_id()==0)s8_phase_init(address(),core,warp);
 s8_fence();return s8_phase_mark(0);
}
#else
int s8_read_mcycle(uint64_t *){return -ENOSYS;}
#endif

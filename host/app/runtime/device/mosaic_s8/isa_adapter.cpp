#include "tile.h"
#if defined(__riscv) && (__riscv_xlen==32)
#include <vx_intrinsics.h>
#include "../s8/isa.h"
static mosaic_words4 gather(uint32_t value){mosaic_words4 v;for(unsigned lane=0;lane<4;++lane)v.word[lane]=vx_shfl_idx(value,lane,3,0);return v;}
static int native_begin(void *,uint32_t k,mosaic_words4 *v){*v=gather(s8_isa_begin(vx_thread_id()==0?k:0));return 0;}
static int native_step(void *,const mosaic_words4 *a,const mosaic_words4 *b){uint32_t lane=vx_thread_id();s8_isa_step(a->word[lane],b->word[lane]);return 0;}
// One ABI call per tile. The original full FPR clobber remains on every
// STEP, allowing callee-saved FPR preservation outside this integer-only loop.
static int native_steps(void *,const int16_t*a,const int16_t*b,uint32_t k){
 const uint32_t lane=vx_thread_id();
 for(uint32_t t=0;t<k;++t){const uint32_t av=mosaic_lane_word(a+t*8,lane),bv=mosaic_lane_word(b+t*8,lane);s8_isa_step(av,bv);}
 return 0;
}
static int native_read(void *,uint32_t control,mosaic_words4 *v){*v=gather(s8_isa_read(vx_thread_id()==0?control:0));return 0;}
// All pages in one integer-only native function. Every lane contributes its
// own32-bit stream to shared WORK; no per-page gather or function call.
static int native_read_pages(void *,uint32_t*raw){
 const uint32_t lane=vx_thread_id();
 for(uint32_t page=0;page<24;++page)raw[page*4+lane]=s8_isa_read(lane==0?page:0);
 return 0;
}
static int native_agree(void *,int value){
 int first=0;
 for(unsigned lane=0;lane<4;++lane){const int rc=int32_t(vx_shfl_idx(uint32_t(value),lane,3,0));if(!first&&rc)first=rc;}
 return first;
}
static int native_synchronize(void *){
 __asm__ volatile("fence iorw, iorw":::"memory");vx_barrier(0,1);
 __asm__ volatile("fence iorw, iorw":::"memory");return 0;
}
static int native_release(void *p,mosaic_words4 *v){return native_read(p,0x11f,v);}
static void native_fence(void *){__asm__ volatile("fence iorw, iorw":::"memory");}
static int native_broadcast(void *,int value){return int32_t(vx_shfl_idx(uint32_t(value),0,3,0));}
static uint64_t native_now(void *){
 uint32_t hi,lo,again;
 do{__asm__ volatile("rdcycleh %0":"=r"(hi));__asm__ volatile("rdcycle %0":"=r"(lo));__asm__ volatile("rdcycleh %0":"=r"(again));}while(hi!=again);
 return (uint64_t(hi)<<32)|lo;
}
mosaic_s8_ops mosaic_native_s8_ops(uint32_t core,uint32_t warp,uint32_t lane,uint32_t polls){
 bool identity=core==vx_core_id()&&warp==vx_warp_id()&&lane==vx_thread_id();
 return {nullptr,native_begin,native_step,native_read,native_release,native_fence,native_now,core,identity?warp:4,lane,polls,0,native_broadcast,native_steps,native_read_pages,native_synchronize,native_agree,nullptr};
}
#endif

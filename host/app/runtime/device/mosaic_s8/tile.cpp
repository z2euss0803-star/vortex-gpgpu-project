#include "tile.h"
#include <errno.h>
#include "profile.h"
static bool same(const mosaic_words4 &v,uint32_t a,uint32_t b,uint32_t c,uint32_t d){return v.word[0]==a&&v.word[1]==b&&v.word[2]==c&&v.word[3]==d;}
static mosaic_words4 pack(const int16_t *p){
 mosaic_words4 v;
 for(unsigned lane=0;lane<4;++lane)v.word[lane]=uint16_t(p[lane*2])|(uint32_t(uint16_t(p[lane*2+1]))<<16);
 return v;
}
#if !defined(__riscv)
static int legacy_tile8(const int16_t *a,const int16_t *b,uint32_t k,int64_t out[64],const mosaic_s8_ops *io){
 if(!a||!b||!out||!io||!io->begin||!io->step||!io->read||!io->release||!io->fence||!io->now||!k||k>4096||io->core>3||io->warp!=0||io->participant>4||!io->poll_limit||io->poll_limit>65536)return -EINVAL;
 if(io->steps&&((reinterpret_cast<uintptr_t>(a)|reinterpret_cast<uintptr_t>(b))&3u))return -EINVAL;
 uint32_t pages[96];int64_t decoded[64];mosaic_words4 value;
 uint64_t start=io->now(io->context);io->fence(io->context);
 int rc=io->begin(io->context,k,&value);if(rc)return rc;
 if(!same(value,0,0,k,1))return -EPROTO;
 if(io->steps){rc=io->steps(io->context,a,b,k);if(rc)return rc;}
 else for(uint32_t t=0;t<k;++t){
  mosaic_words4 av=pack(a+t*8),bv=pack(b+t*8);
  rc=io->step(io->context,&av,&bv);if(rc)return rc;
 }
 bool ready=false;
 for(uint32_t poll=0;poll<io->poll_limit;++poll){
  if(io->deadline_ticks&&io->now(io->context)-start>io->deadline_ticks)return -ETIMEDOUT;
  rc=io->read(io->context,31,&value);if(rc)return rc;
  if(same(value,0,k,k,3)){ready=true;break;}
  if(!same(value,4,k,k,1))return -EPROTO;
 }
 if(!ready)return -ETIMEDOUT;
 for(uint32_t page=0;page<24;++page){
  rc=io->read(io->context,page,&value);if(rc)return rc;
  for(unsigned lane=0;lane<4;++lane)pages[page*4+lane]=value.word[lane];
 }
 rc=io->read(io->context,31,&value);if(rc)return rc;
 if(!same(value,0,k,k,7))return -EPROTO;
 rc=mosaic_unpack48(pages,decoded);if(rc)return rc;
 // Release only a fully drained result. On any earlier fault, caller retains
 // ownership/quarantines; do not guess a reset or release an in-flight array.
 rc=io->release(io->context,&value);if(rc)return rc;
 if(!same(value,0,k,k,7))return -EPROTO;
 rc=io->read(io->context,31,&value);if(rc)return rc;
 if(!same(value,0,0,0,0))return -EPROTO;
 io->fence(io->context);
 if(io->participant==0||io->participant==4)for(unsigned i=0;i<64;++i)out[i]=decoded[i];
 io->fence(io->context);return 0;
}

#endif
// Every error-producing operation is followed by the same fixed agreement on
// all participants. A lane never leaves its peers inside a TCU collective.
static int agreement(const mosaic_s8_ops*io,int rc){return io->agree(io->context,rc);}
static int synchronization(const mosaic_s8_ops*io){return agreement(io,io->synchronize(io->context));}
int mosaic_tile8(const int16_t*a,const int16_t*b,uint32_t k,int64_t out[64],const mosaic_s8_ops*io){
 #if !defined(__riscv)
 if(!io||!io->read_pages)return legacy_tile8(a,b,k,out,io);
#else
 if(!io||!io->read_pages)return -EINVAL;
#endif
 if(!io->agree||!io->synchronize||io->participant>3)return -EINVAL;
 int rc=!a||!b||!out||!io->raw_pages||!io->begin||!io->step||!io->steps||!io->read||!io->release||!io->fence||!io->now||!k||k>4096||io->core>3||io->warp||!io->poll_limit||io->poll_limit>65536?-EINVAL:0;
 if(!rc&&((reinterpret_cast<uintptr_t>(a)|reinterpret_cast<uintptr_t>(b)|reinterpret_cast<uintptr_t>(io->raw_pages))&3u))rc=-EINVAL;
 rc=agreement(io,rc);if(rc)return rc;
 const uint64_t start=io->now(io->context);mosaic_words4 value{};
 rc=synchronization(io);if(rc)return rc;
 uint64_t phase_start=mosaic_profile_start(*io);
 rc=io->begin(io->context,k,&value);if(!rc&&!same(value,0,0,k,1))rc=-EPROTO;
 rc=agreement(io,rc);if(rc)return rc;
 rc=agreement(io,io->steps(io->context,a,b,k));if(rc)return rc;
 bool ready=false;
 for(uint32_t poll=0;poll<io->poll_limit;++poll){
  rc=io->participant==0&&io->deadline_ticks&&io->now(io->context)-start>io->deadline_ticks?-ETIMEDOUT:0;
  rc=agreement(io,rc);if(rc)return rc;
  rc=io->read(io->context,31,&value);
  if(!rc){ready=same(value,0,k,k,3);if(!ready&&!same(value,4,k,k,1))rc=-EPROTO;}
  rc=agreement(io,rc);if(rc)return rc;
  if(ready)break;
 }
 rc=agreement(io,ready?0:-ETIMEDOUT);if(rc)return rc;
 mosaic_profile_stop(*io,MOSAIC_STAGE_BEGIN_STEP_V1,phase_start,k,uint64_t(k)*32);
 phase_start=mosaic_profile_start(*io);
 rc=agreement(io,io->read_pages(io->context,io->raw_pages));if(rc)return rc;
 rc=synchronization(io);if(rc)return rc;
 rc=io->read(io->context,31,&value);if(!rc&&!same(value,0,k,k,7))rc=-EPROTO;
 rc=agreement(io,rc);if(rc)return rc;
 // A failure before this point keeps the array owned/quarantined. Release is
 // attempted exactly once after all pages and DRAIN, never as fault cleanup.
 rc=io->release(io->context,&value);if(!rc&&!same(value,0,k,k,7))rc=-EPROTO;
 rc=agreement(io,rc);if(rc)return rc;
 rc=io->read(io->context,31,&value);if(!rc&&!same(value,0,0,0,0))rc=-EPROTO;
 rc=agreement(io,rc);if(rc)return rc;
 const uint32_t first=io->participant*16;int64_t decoded[16];
 rc=agreement(io,mosaic_unpack48_range(io->raw_pages,decoded,first,16));if(rc)return rc;
 for(unsigned i=0;i<16;++i)out[first+i]=decoded[i];
 rc=synchronization(io);if(rc)return rc;
 mosaic_profile_stop(*io,MOSAIC_STAGE_READ_DECODE_V1,phase_start,24,896);return 0;
}

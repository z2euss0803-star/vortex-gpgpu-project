#include "graph_internal.h"
#include <math.h>
#include "profile.h"
#include "reuse_schedule_v2.h"
static int restore(int64_t q,float sw,float sa,float bias,float *out){
 if(q<-(INT64_C(1)<<47)||q>=(INT64_C(1)<<47)||!isfinite(sw)||sw<=0||!isfinite(bias))return -ERANGE;
 volatile float f=float(q),weighted=f*sw,scaled=weighted*sa,value=scaled+bias;
 if(!isfinite(value))return -ERANGE;
 *out=value;return 0;
}
#if !defined(__riscv)
static int legacy_convolve_range(const mosaic_job_v1 *job,mosaic_device_context *c,const mosaic_layer *geometry){
 mosaic_layer l=*geometry;auto &m=c->memory;auto &p=c->parameters[job->layer];auto &io=c->io[c->core];
 l.quantized=m.quantized;l.quantized_count=size_t(l.ci)*l.h*l.w;
 uint32_t spatial=mosaic_oh(l)*mosaic_ow(l),nblocks=(spatial+7)/8,inner=l.ci*l.k*l.k;
 uint32_t last_m=UINT32_MAX;
 for(uint32_t tile=job->tile_first;tile<job->tile_first+job->tile_count;++tile){
  if(tile%4!=c->core)continue;
  uint32_t m0=(tile/nblocks)*8,n0=(tile%nblocks)*8;int rc=0;
  if(mosaic_leader(c)){
   rc=mosaic_pack_tile(&l,n0,0,m.b[c->core],m.tile_capacity);
   if(!rc&&last_m!=m0){++c->weight_packs;for(uint32_t kk=0;kk<l.kpad;++kk)for(unsigned row=0;row<8;++row)m.a[c->core][kk*8+row]=m0+row<l.co?p.weights[size_t(m0+row)*l.kpad+kk]:0;last_m=m0;}
  }
  rc=mosaic_share(c,rc);if(rc)return rc;io.fence(io.context);
  rc=mosaic_tile8(m.a[c->core],m.b[c->core],inner,m.tile[c->core],&io);if(rc)return rc;
  if(mosaic_leader(c)){
   ++c->tiles;
   for(unsigned row=0;row<8&&!rc;++row)for(unsigned n=0;n<8&&!rc;++n)if(m0+row<l.co&&n0+n<spatial)rc=restore(m.tile[c->core][row*8+n],p.coeff[(m0+row)*2],p.sa,p.coeff[(m0+row)*2+1],m.fp[(job->layer+1)%2]+size_t(m0+row)*spatial+n0+n);
  }
  rc=mosaic_share(c,rc);if(rc)return rc;
 }
 return 0;
}
#endif

int mosaic_convolve_range(const mosaic_job_v1*job,mosaic_device_context*c,const mosaic_layer*geometry){
 const bool reuse=c->schedule_version==2&&c->weight_layout_version==2;
 if(!reuse&&(c->schedule_version!=1||c->weight_layout_version!=1))return -EINVAL;
 auto&io=c->io[c->core];
#if !defined(__riscv)
 if(io.participant==4||!io.read_pages)return reuse?-EINVAL:legacy_convolve_range(job,c,geometry);
#else
 if(io.participant==4||!io.read_pages)return -EINVAL;
#endif
 if(!io.agree||!io.synchronize||io.participant>3)return -EINVAL;
 auto&m=c->memory;auto&p=c->parameters[job->layer];mosaic_layer l=*geometry;
 l.quantized=m.quantized;l.quantized_count=size_t(l.ci)*l.h*l.w;
 const uint32_t spatial=mosaic_oh(l)*mosaic_ow(l),inner=l.ci*l.k*l.k;
 const unsigned lane=io.participant;uint32_t last_m=UINT32_MAX,last_nb=UINT32_MAX;
 mosaic_reuse::ScheduleShape schedule{};int admitted=mosaic_reuse::make_schedule(l.co,spatial,reuse?mosaic_reuse::ScheduleId::SpatialMajorV1:mosaic_reuse::ScheduleId::LegacyChannelMajorV1,&schedule);
 if(!admitted)admitted=mosaic_reuse::validate_job(schedule,job->tile_first,job->tile_count);
 admitted=io.agree(io.context,admitted);if(admitted)return admitted;
 for(uint32_t tile=job->tile_first;tile<job->tile_first+job->tile_count;++tile){
  mosaic_reuse::TileAddress address{};int rc=mosaic_reuse::map_tile(schedule,tile,&address);if(rc)return rc;
  if(address.owner!=c->core)continue;
  const uint32_t m0=address.m0,n0=address.n0;
  uint64_t start=0;
  const uint32_t valid_n=spatial-n0<8?spatial-n0:8;
  // Job-local only: never inherit a B window across invocations or inputs.
  if(!reuse||last_nb!=address.nb){
   start=mosaic_profile_start(io);
   rc=mosaic_pack_tile_lane(&l,n0,0,m.b[c->core],m.tile_capacity,lane);
   rc=io.agree(io.context,rc);if(rc)return rc;
   rc=io.agree(io.context,io.synchronize(io.context));if(rc)return rc;
   last_nb=address.nb;
   mosaic_profile_stop(io,MOSAIC_STAGE_INPUT_PACK_V1,start,uint64_t(inner)*valid_n,uint64_t(inner)*valid_n*2+uint64_t(l.kpad)*16);
  }
  if(last_m!=m0){
   start=mosaic_profile_start(io);
   if(reuse){
    // Four contiguous pieces of the derived block enter owned writable A.
    // Even co3 reads the complete validated padded8-row source extent.
    const size_t chunk=size_t(l.kpad)*2,begin=lane*chunk,end=begin+chunk;
    const int16_t*source=p.weights+size_t(address.mb)*l.kpad*8;
    for(size_t at=begin;at<end;++at)m.a[c->core][at]=source[at];
   }else for(uint32_t kk=0;kk<l.kpad;++kk)for(unsigned row=lane*2;row<lane*2+2;++row)m.a[c->core][kk*8+row]=m0+row<l.co?p.weights[size_t(m0+row)*l.kpad+kk]:0;
   rc=io.agree(io.context,io.synchronize(io.context));if(rc)return rc;
   last_m=m0;if(!lane)++c->weight_packs;
   const uint32_t valid_m=l.co-m0<8?l.co-m0:8;
   mosaic_profile_stop(io,MOSAIC_STAGE_WEIGHT_PREPARE_V1,start,uint64_t(l.kpad)*(reuse?8:valid_m),uint64_t(l.kpad)*(reuse?16:valid_m+8)*2);
  }
  rc=mosaic_tile8(m.a[c->core],m.b[c->core],inner,m.tile[c->core],&io);if(rc)return rc;
  start=mosaic_profile_start(io);float prepared[16];unsigned valid=0;
  for(unsigned i=lane*16;i<lane*16+16;++i){
   const uint32_t row=i/8,n=i%8;
   if(m0+row<l.co&&n0+n<spatial){int error=restore(m.tile[c->core][i],p.coeff[(m0+row)*2],p.sa,p.coeff[(m0+row)*2+1],prepared+i-lane*16);if(!rc&&error)rc=error;++valid;}
  }
  rc=io.agree(io.context,rc);if(rc)return rc;
  for(unsigned i=lane*16;i<lane*16+16;++i){const uint32_t row=i/8,n=i%8;if(m0+row<l.co&&n0+n<spatial)m.fp[(job->layer+1)%2][size_t(m0+row)*spatial+n0+n]=prepared[i-lane*16];}
  rc=io.agree(io.context,io.synchronize(io.context));if(rc)return rc;
  if(!lane)++c->tiles;
  const uint32_t valid_m=l.co-m0<8?l.co-m0:8;
  mosaic_profile_stop(io,MOSAIC_STAGE_OUTPUT_RESTORE_V1,start,uint64_t(valid_m)*valid_n,uint64_t(valid_m)*valid_n*20);
  (void)valid;
 }
 return 0;
}

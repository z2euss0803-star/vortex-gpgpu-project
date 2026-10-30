#include "norm_compat.h"
#include "graph_internal.h"
#include <vector>
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cerrno>
int main(){
 const unsigned channels=5;
 for(unsigned spatial:{4096u,16384u,513u}){
  size_t work_count=1;while(work_count<spatial)work_count*=2;
  std::vector<float> x(channels*spatial),reference(channels*spatial),hi[4],lo[4],stats(channels*3),rhi(work_count),rlo(work_count);
  float gamma[]={1.125f,-.875f,.25f,-1.5f,2.f},beta[]={.125f,.03125f,-.75f,.5f,1.f};
  for(size_t i=0;i<x.size();i++)x[i]=float(int(i%113)-56)*.013f+float(i%17)*.00001f;
  reference=x;mosaic_norm_workspace rw={rhi.data(),rlo.data(),work_count};float rs[3];
  for(unsigned ch=0;ch<channels;ch++)assert(!mosaic_norm_stats_compat_v3(reference.data()+ch*spatial,spatial,&rw,rs)&&!mosaic_norm_apply_compat_v3(reference.data()+ch*spatial,spatial,rs,gamma[ch],beta[ch],spatial,reference.data()+ch*spatial));
  mosaic_device_context c={};c.memory.fp[1]=x.data();c.memory.stats=stats.data();c.parameters[0].gamma=gamma;c.parameters[0].beta=beta;
  for(unsigned i=0;i<4;i++){hi[i].resize(work_count);lo[i].resize(work_count);c.memory.norm[i]={hi[i].data(),lo[i].data(),work_count};c.io[i].participant=4;}
  mosaic_layer layer={1,channels,1,spatial,1,1,1,32,nullptr,0};mosaic_job_v1 job={};job.op=3;job.tile_count=channels;
  for(unsigned core=0;core<4;core++){c.core=core;assert(!mosaic_point_range(&job,&c,&layer));}
  job.op=4;job.tile_count=(channels*spatial+255)/256;
  for(unsigned core=0;core<4;core++){c.core=core;assert(!mosaic_point_range(&job,&c,&layer));}
  assert(!memcmp(x.data(),reference.data(),x.size()*4));
  assert(mosaic_norm_apply_compat_v3(x.data(),257,stats.data(),1,0,256,x.data())==-EINVAL);
  assert(mosaic_norm_apply_compat_v3(x.data(),1,stats.data(),1,0,0,x.data())==-EINVAL);
  assert(mosaic_norm_apply_compat_v3(x.data(),256,stats.data(),1,0,spatial,x.data()+1)==-EINVAL);
  x[0]=NAN;assert(mosaic_norm_stats_compat_v3(x.data(),spatial,&rw,rs)==-EDOM);
 }
 std::vector<float> constant(4096,7),hi(4096),lo(4096);float s[3];mosaic_norm_workspace ws={hi.data(),lo.data(),4096};
 assert(!mosaic_norm_stats_compat_v3(constant.data(),4096,&ws,s)&&s[0]==7&&s[1]==0);
 assert(!mosaic_norm_apply_compat_v3(constant.data(),4096,s,2,3,4096,constant.data()));
 for(float v:constant)assert(std::isfinite(v));
 mosaic_norm_workspace alias={hi.data(),hi.data(),4096};assert(mosaic_norm_stats_compat_v3(constant.data(),4096,&alias,s)==-EINVAL);
 puts("NORM_V3_FOUR_CORE_TILE_CHUNK_MODE_ERROR_CHECKS_PASS_NOT_GPU raw_gpu_cycles=null");
}

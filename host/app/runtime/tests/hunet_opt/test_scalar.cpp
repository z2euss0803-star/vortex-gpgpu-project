#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include <sys/mman.h>
#include "../../device/hunet_opt_scalar.h"
#include "../../vendor/hunet/v5/include/hunet_ops_baseline.h"
int main(){
 constexpr size_t bytes=32*1024*1024;void *pool=mmap(nullptr,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);assert(pool!=MAP_FAILED&&uintptr_t(pool)+bytes<=UINT32_MAX);
 float*x=static_cast<float*>(pool),*w=x+1048576,*out=w+1048576,*ref=out+1048576,*gamma=ref+1048576,*beta=gamma+4096,*stats=beta+4096;
 for(unsigned i=0;i<1048576;i++){x[i]=float(int(i%503)-251)/79;w[i]=float(int(i%127)-63)/31;}
 for(unsigned i=0;i<4096;i++){gamma[i]=float(i%13)/8;beta[i]=float(i%7)/8;}stats[0]=0.125f;stats[1]=0.75f;
 std::ifstream f("generated/hunet_fused/jobs.bin",std::ios::binary|std::ios::ate);assert(f);auto count=size_t(f.tellg())/128;std::vector<HunetJobV1>jobs(count);f.seekg(0);f.read(reinterpret_cast<char*>(jobs.data()),count*128);assert(f);
 unsigned covered=0;
 for(auto j:jobs){if(!opt_scalar_supported(j.op))continue;covered++;j.x=uint32_t(uintptr_t(x));j.w=uint32_t(uintptr_t(w));j.gamma=uint32_t(uintptr_t(gamma));j.beta=uint32_t(uintptr_t(beta));j.scratch=uint32_t(uintptr_t(stats));j.y=uint32_t(uintptr_t(ref));assert(opt_scalar_job_valid(j));
  for(unsigned t=0;t<j.tasks;t++){hunet_scalar_baseline(&j,t);}
  j.y=uint32_t(uintptr_t(out));
  for(unsigned chunk:{1u,4u,16u}){std::fill(out,out+j.tasks+1,123.0f);unsigned grid=opt_scalar_grid(j.tasks,chunk);
   for(unsigned block=0;block<grid;block++)for(unsigned lane=0;lane<4;lane++){auto range=opt_scalar_range(j.tasks,block,lane,chunk);if(range.valid)assert(!opt_scalar_span(j,range.first,range.end,4));}
   assert(!std::memcmp(out,ref,j.tasks*4)&&out[j.tasks]==123);
  }
 }
 assert(covered==52);
 // Explicit one-element and non-divisible tails in addition to full frozen shapes.
 for(unsigned n:{1u,13u,17u}){
  HunetJobV1 tail={};tail.op=8;tail.dtype=1;tail.ci=tail.co=1;tail.ih=tail.oh=1;tail.iw=tail.ow=tail.tasks=n;
  tail.x=uint32_t(uintptr_t(x));tail.w=uint32_t(uintptr_t(w));tail.y=uint32_t(uintptr_t(ref));
  for(unsigned t=0;t<n;t++)hunet_scalar_baseline(&tail,t);
  tail.y=uint32_t(uintptr_t(out));
  for(unsigned chunk:{1u,4u,16u}){
   std::fill(out,out+n+1,123.0f);
   for(unsigned block=0;block<opt_scalar_grid(n,chunk);block++)for(unsigned lane=0;lane<4;lane++){
    auto r=opt_scalar_range(n,block,lane,chunk);if(r.valid)assert(!opt_scalar_span(tail,r.first,r.end,4));
   }
   assert(!std::memcmp(out,ref,n*4)&&out[n]==123.0f);
  }
 }
 HunetJobV1 j={};j.op=8;j.dtype=1;j.ci=j.co=j.ih=j.iw=j.oh=j.ow=j.tasks=1;j.x=uint32_t(uintptr_t(x));j.w=uint32_t(uintptr_t(w));j.y=uint32_t(uintptr_t(out));x[0]=INFINITY;assert(opt_scalar_span(j,0,1,1));j.tasks=0;assert(!opt_scalar_job_valid(j));assert(!opt_scalar_grid(0,1)&&!opt_scalar_grid(1,2));assert(!opt_scalar_range(1,UINT32_MAX,3,16).valid);
 assert(!munmap(pool,bytes));puts("HOST_LOW_VA_FIXTURE_ALL_52_FROZEN_SCALAR_JOBS_CHUNK1_4_16_EXACT_AND_FINITE_PASS_NOT_GPU");return 0;
}

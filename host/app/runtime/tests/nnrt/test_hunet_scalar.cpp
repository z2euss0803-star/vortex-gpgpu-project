#include "device/vxnn/hunet_ops.h"
#include "vendor/hunet/v5/include/hunet_ops_baseline.h"
#include <sys/mman.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
static void need(bool b,const char*s){if(!b){std::fprintf(stderr,"U12_SCALAR_ASSERT %s\n",s);std::exit(66);}}
// Only the independent, unmodified legacy reference needs low host addresses.
// Verify the complete span before ever putting an address into its 32-bit wire.
struct Low {
 float*p;size_t n;
 explicit Low(size_t count):p(nullptr),n(count){
  int flags=MAP_PRIVATE|MAP_ANONYMOUS;
#ifdef MAP_32BIT
  flags|=MAP_32BIT;
#endif
  void*q=mmap(nullptr,n*4,PROT_READ|PROT_WRITE,flags,-1,0);
  need(q!=MAP_FAILED&&reinterpret_cast<uintptr_t>(q)<=UINT32_MAX-n*4,"legacy reference low-address allocation");p=static_cast<float*>(q);
 }
 ~Low(){munmap(p,n*4);}
 Low(const Low&)=delete;Low&operator=(const Low&)=delete;
 uint32_t wire()const{return uint32_t(reinterpret_cast<uintptr_t>(p));}
};
static void run(HunetJobV1 j,uint64_t&values){
 const size_t hw=size_t(j.ih)*j.iw,ohw=size_t(j.oh)*j.ow;
 size_t nx=j.ci*hw,ny=j.co*ohw,nw=0,ng=0,ns=0;
 if(j.op==HUNET_GN_STATS)ny=2;
 if(j.op==HUNET_GN_APPLY){ng=j.ci;ns=2;}
 if(j.op==HUNET_ADD)nw=ny;
 if(j.op==HUNET_MUL_CHANNEL)nw=j.co;
 if(j.op==HUNET_CONCAT)nw=ny-nx;
 Low x(nx),w(nw?nw:1),gamma(ng?ng:1),beta(ng?ng:1),stats(2),old(ny+2);
 for(size_t i=0;i<nx;i++)x.p[i]=i%11==0?-0.0f:float(int(i%139)-69)/31;
 for(size_t i=0;i<nw;i++)w.p[i]=float(int(i%71)-35)/29;
 for(size_t i=0;i<ng;i++){gamma.p[i]=float(int(i%7)-3)/9;beta.p[i]=float(int(i%13)-6)/11;}
 stats.p[0]=.1234567f;stats.p[1]=.7654321f;
 std::vector<float>out(ny+2,12345.0f);std::fill(old.p,old.p+ny+2,12345.0f);
 HunetJobV1 ref=j;ref.x=x.wire();ref.y=old.wire()+4;ref.w=w.wire();ref.gamma=gamma.wire();ref.beta=beta.wire();ref.scratch=stats.wire();
 for(uint32_t t=0;t<j.tasks;t++)hunet_scalar_baseline(&ref,t);
 j.x=j.w=j.bias=j.gamma=j.beta=j.y=j.scratch=j.aux=0;
 VxnnHunetScalarBuffers b{{x.p,nx},{nw?w.p:nullptr,nw},{ng?gamma.p:nullptr,ng},{ng?beta.p:nullptr,ng},{ns?stats.p:nullptr,ns},{out.data()+1,ny}};
 // A range boundary deliberately cuts channels/rows, preserving the old order.
 uint32_t cut=j.tasks/3;
 if(cut)need(!vxnn_hunet_scalar(&j,&b,0,cut),"new scalar first span executes");
 need(!vxnn_hunet_scalar(&j,&b,cut,j.tasks-cut),"new scalar remainder executes");
 need(!std::memcmp(out.data(),old.p,(ny+2)*4),"all outputs including negative zero and guards bitwise equal actual old source");values+=ny;
 auto saved=out;auto bad=b;bad.y.count=ny-1;need(vxnn_hunet_scalar(&j,&bad,0,j.tasks)<0&&saved==out,"short output rejected before writing");
 bad=b;bad.x.count=nx-1;need(vxnn_hunet_scalar(&j,&bad,0,j.tasks)<0&&saved==out,"short input rejected before writing");
 bad=b;bad.y.data=x.p;need(vxnn_hunet_scalar(&j,&bad,0,j.tasks)<0,"input output alias rejected");
 need(vxnn_hunet_scalar(&j,&b,j.tasks,1)<0&&saved==out,"out of range task rejected");
 need(vxnn_hunet_scalar(&j,&b,UINT32_MAX,UINT32_MAX)<0&&saved==out,"task range overflow rejected");
 need(vxnn_hunet_scalar(&j,&b,0,0)<0&&saved==out,"empty task range rejected");
 bad=b;bad.x.data=reinterpret_cast<const float*>(~uintptr_t(3));need(vxnn_hunet_scalar(&j,&bad,0,j.tasks)<0&&saved==out,"wrapping pointer span rejected before access");
 bad=b;bad.x.data=reinterpret_cast<const float*>(reinterpret_cast<const char*>(x.p)+1);need(vxnn_hunet_scalar(&j,&bad,0,j.tasks)<0&&saved==out,"unaligned input rejected");
 auto invalid=j;invalid.x=ref.x;need(vxnn_hunet_scalar(&invalid,&b,0,j.tasks)<0&&saved==out,"legacy address cannot cross typed boundary");
 invalid=j;invalid.tasks++;need(vxnn_hunet_scalar(&invalid,&b,0,1)<0&&saved==out,"task shape mismatch rejected");
 invalid=j;invalid.op=HUNET_GELU;need(vxnn_hunet_scalar(&invalid,&b,0,1)<0&&saved==out,"GPU scalar path cannot consume CPU GELU");
 invalid=j;invalid.ci=UINT32_MAX;need(vxnn_hunet_scalar(&invalid,&b,0,1)<0&&saved==out,"overflow shape rejected");
 if(ng){bad=b;bad.gamma.count--;need(vxnn_hunet_scalar(&j,&bad,0,j.tasks)<0&&saved==out,"short affine vector rejected");}
 if(ns){bad=b;bad.stats.count--;need(vxnn_hunet_scalar(&j,&bad,0,j.tasks)<0&&saved==out,"short stats rejected");}
 else {bad=b;bad.stats={stats.p,2};need(vxnn_hunet_scalar(&j,&bad,0,j.tasks)<0&&saved==out,"unused reference cannot smuggle an operand");}
 if(nw){bad=b;bad.w.count--;need(vxnn_hunet_scalar(&j,&bad,0,j.tasks)<0&&saved==out,"short second operand rejected");}
 if(j.op==HUNET_SHIFT){invalid=j;invalid.n_div=0;need(vxnn_hunet_scalar(&invalid,&b,0,j.tasks)<0&&saved==out,"division by zero rejected");}
 if(j.op==HUNET_GN_STATS){invalid=j;invalid.epsilon_bits=0;need(vxnn_hunet_scalar(&invalid,&b,0,1)<0&&saved==out,"zero epsilon rejected");}
 if(j.op==HUNET_SLICE){invalid=j;invalid.groups=UINT32_MAX;need(vxnn_hunet_scalar(&invalid,&b,0,1)<0&&saved==out,"slice base overflow rejected");}
 if(j.op==HUNET_COPY){x.p[0]=INFINITY;need(vxnn_hunet_scalar(&j,&b,0,1)<0,"nonfinite result returns runtime failure, partial output not published as success");}
}
int main(){
 std::ifstream f("generated/hunet_fused/jobs.bin",std::ios::binary);need(bool(f),"frozen original schedule open");
 HunetJobV1 j;unsigned jobs=0;uint64_t values=0;
 while(f.read(reinterpret_cast<char*>(&j),sizeof(j))){if(j.op==15)continue;run(j,values);jobs++;}
 need(f.eof()&&f.gcount()==0&&jobs==68,"all 68 frozen scalar jobs read completely");
 j={};j.magic=HUNET_MAGIC;j.abi=1;j.bytes=128;j.op=HUNET_COPY;j.dtype=1;j.tasks=15;j.ci=j.co=3;j.ih=j.oh=1;j.iw=j.ow=5;j.nitems=15;run(j,values);
 std::printf("U12_SCALAR_68_FROZEN_JOBS_PLUS_COPY_%llu_VALUES_BITWISE_LEGACY_NOT_GPU gpu_raw_cycles=null\n",(unsigned long long)values);
}

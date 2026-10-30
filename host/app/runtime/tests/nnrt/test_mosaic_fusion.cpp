#include "device/vxnn/mosaic_ops.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fenv.h>
#include <limits>
#include <vector>
extern "C" int fusion_host_dispatch(void *);
namespace {
void need(bool yes,const char *why){if(!yes){std::fprintf(stderr,"MOSAIC_FUSION_ASSERT %s\n",why);std::exit(71);}}
struct Dispatch {VxnnMosaicPrimitive p;VxnnMosaicBuffers b;unsigned owners[4][4]{};int rc[4]{};};
}
extern "C" void fusion_host_owner(unsigned core,unsigned warp,unsigned lane,int rejected,void *opaque){
 auto&d=*static_cast<Dispatch*>(opaque);need(!rejected&&!warp&&core<4&&lane<4,"actual entry owner identity");
 ++d.owners[core][lane];if(!lane)d.rc[core]=vxnn_mosaic_primitive(&d.p,&d.b,core,nullptr);
}
namespace {
void run(Dispatch&d){
 need(!fusion_host_dispatch(&d),"actual SDK launch");
 for(unsigned c=0;c<4;++c)for(unsigned lane=0;lane<4;++lane)need(d.owners[c][lane]==1,"all actual SDK owner lanes exactly once");
 for(unsigned c=0;c<4;++c)if(d.rc[c]){std::fprintf(stderr,"opcode=%u core=%u rc=%d\n",d.p.opcode,c,d.rc[c]);need(false,"fused primitive must accept the canonical bounded operation");}
}
void oracle(const VxnnMosaicPrimitive&p,const VxnnMosaicBuffers&base,bool add,std::vector<int16_t>&q,std::vector<float>&saved){
 const auto&g=p.geometry;size_t spatial=size_t((g.h*g.upsample+g.stride-1)/g.stride)*((g.w*g.upsample+g.stride-1)/g.stride),elements=size_t(g.co)*spatial;
 std::vector<float>norm(elements,123.25f),act(elements,123.25f);auto b=base;auto old=p;
 old.opcode=4;old.scale=0;old.save_input=false;old.separate_input=true;b.output={norm.data(),elements};
 for(unsigned c=0;c<4;++c)need(!vxnn_mosaic_primitive(&old,&b,c,nullptr),"independent original NormApply");
 old.opcode=add?6:5;b.input={norm.data(),elements};b.output={act.data(),elements};
 for(unsigned c=0;c<4;++c)need(!vxnn_mosaic_primitive(&old,&b,c,nullptr),"independent original activation");
 old.opcode=1;old.scale=p.scale;old.save_input=p.save_input;old.separate_input=false;
 old.geometry={g.co,1,(g.h*g.upsample+g.stride-1)/g.stride,(g.w*g.upsample+g.stride-1)/g.stride,3,1,1,(g.co*9+31)/32*32};
 b.input={act.data(),elements};b.quant={q.data()+8,elements};b.skip={saved.data()+8,elements};
 for(unsigned c=0;c<4;++c)need(!vxnn_mosaic_primitive(&old,&b,c,nullptr),"independent original Quant and optional save");
}
unsigned cases=0;
void one(unsigned h,unsigned w,unsigned co,unsigned first,unsigned count,bool save,bool add,float scale){
 size_t elements=size_t(co)*h*w;
 std::vector<float>x(elements),skip(elements),stats(co*3),gamma(co),beta(co);
 for(size_t i=0;i<elements;++i){x[i]=float(int(i%127)-63)*.125f;skip[i]=float(int(i%37)-18)*.0625f;}
 const float edge[]={0.f,-0.f,.5f,-.5f,1.5f,-1.5f,32767.f,-32767.f,std::numeric_limits<float>::denorm_min(),-std::numeric_limits<float>::denorm_min()};
 for(size_t i=0;i<std::min(elements,sizeof(edge)/sizeof(edge[0]));++i)x[i]=edge[i];
 for(unsigned c=0;c<co;++c){stats[c*3]=float(int(c%3)-1)*.125f;stats[c*3+1]=.25f;stats[c*3+2]=c%2?1.5f:.5f;gamma[c]=c%3?1.25f:-.75f;beta[c]=float(c%7)*.25f;}
 std::vector<int16_t>q(elements+16,-12345),expected=q;
 std::vector<float>saved(elements+16,321.25f),expected_saved=saved;
 Dispatch d;d.p={add?8u:7u,first,count,{1,co,h,w,3,1,1,32},scale,save,false};
 d.b.input={x.data(),elements};d.b.quant={q.data()+8,elements};d.b.output={saved.data()+8,elements};d.b.skip={skip.data(),elements};d.b.stats={stats.data(),stats.size()};d.b.gamma={gamma.data(),gamma.size()};d.b.beta={beta.data(),beta.size()};
 oracle(d.p,d.b,add,expected,expected_saved);run(d);
 need(q==expected,"full INT16 bytes incl untouched items and guards match old three-stage oracle");
 need(!std::memcmp(saved.data(),expected_saved.data(),saved.size()*4),"saved FP32 bits incl guards match old oracle");
 ++cases;
}
void rejections(bool add){
 float x[16]{},skip[16]{},stats[]={0,1,1},gamma=1,beta=0,saved[16];int16_t q[16];
 std::fill(q,q+16,int16_t(23456));std::fill(saved,saved+16,123.25f);
 VxnnMosaicPrimitive p{add?8u:7u,0,1,{1,1,4,4,3,1,1,32},1,true,false};
 VxnnMosaicBuffers b;b.input={x,16};b.quant={q,16};b.output={saved,16};b.skip={skip,16};b.stats={stats,3};b.gamma={&gamma,1};b.beta={&beta,1};
 auto rejected=[&](const VxnnMosaicPrimitive&v,const VxnnMosaicBuffers&buffers){need(vxnn_mosaic_primitive(&v,&buffers,0,nullptr)<0,"invalid fused call rejected");for(auto n:q)need(n==23456,"rejected first item leaves quant sentinel");for(auto f:saved)need(f==123.25f,"rejected first item leaves save sentinel");};
 auto bad=p;bad.scale=0;rejected(bad,b);bad=p;bad.scale=-1;rejected(bad,b);bad=p;bad.count=4097;rejected(bad,b);
 bad=p;bad.separate_input=true;rejected(bad,b);
 auto alias=b;alias.output={x,16};rejected(p,alias);alias=b;alias.quant={reinterpret_cast<int16_t*>(x),16};rejected(p,alias);
 if(add){alias=b;alias.output={skip,16};rejected(p,alias);}
 x[7]=NAN;rejected(p,b);x[7]=0;stats[2]=INFINITY;rejected(p,b);stats[2]=1;
 need(!fesetround(FE_DOWNWARD),"set nonRNE");rejected(p,b);need(!fesetround(FE_TONEAREST),"restore RNE");
}
}
int main(int argc,char**argv){
 need(argc==2,"relu or add stage");bool add=!std::strcmp(argv[1],"add");need(add||!std::strcmp(argv[1],"relu"),"known stage");
 for(float scale:{.125f,1.f,3.25f})for(bool save:{false,true}){
  one(8,12,3,0,1,save,add,scale);one(8,12,3,1,1,save,add,scale);
  one(2,2,128,1,1,save,add,scale);one(64,64,5,63,17,save,add,scale);
 }
 for(unsigned count:{1u,64u,256u,4096u})one(128,128,128,3,count,true,add,.125f);
 rejections(add);
 std::printf("MOSAIC_FUSION_%s_EXACT_PASS_ACTUAL_SDK_HOST_NOT_GPU cases=%u\n",add?"ADD":"RELU",cases);
}

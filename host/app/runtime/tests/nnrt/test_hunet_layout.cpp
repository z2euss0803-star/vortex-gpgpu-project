#include "device/vxnn/hunet_ops.h"
#include "device/hunet_fused_ops.h"
#include "vendor/hunet/v5/include/hunet_ordered_reductions.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
static void need(bool b,const char*s){if(!b){std::fprintf(stderr,"U12_LAYOUT_ASSERT %s\n",s);std::exit(65);}}
template<class T>static std::vector<T>read(const char*name){std::ifstream f(name,std::ios::binary|std::ios::ate);need(bool(f),"open frozen generated binary");auto n=f.tellg();need(n>0&&n%sizeof(T)==0,"frozen wire multiple");std::vector<T>v(size_t(n)/sizeof(T));f.seekg(0);f.read(reinterpret_cast<char*>(v.data()),n);need(bool(f),"read complete wire");return v;}
static void pack(){
 auto jobs=read<HunetFusedStageV1>("generated/hunet_fused/jobs.bin");auto ds=read<HunetFusedDescriptorV1>("generated/hunet_fused/descriptors.bin");unsigned stages=0,descriptors=0;uint64_t values=0;
 const float special[]={0.0f,-0.0f,65504.0f,-65504.0f,1.00048828125f,1.00146484375f,0.000000059604644775390625f,-0.0000000298023223876953125f};
 for(const auto&s:jobs){if(s.op!=15)continue;++stages;need(s.stage_descriptors_gpu>=0x86d0df80&&!(s.stage_descriptors_gpu%64),"frozen descriptor offset");size_t base=(s.stage_descriptors_gpu-0x86d0df80)/64;need(base+s.descriptor_count<=ds.size(),"bounded frozen descriptors");std::vector<float>x(uint64_t(s.ci)*s.ih*s.iw);for(size_t i=0;i<x.size();i++)x[i]=i%13<8?special[i%13]:float(int(i%997)-498)/31;
  for(unsigned di=0;di<s.descriptor_count;di++){const auto&d=ds[base+di];++descriptors;std::vector<uint16_t>old(uint64_t(d.kp)*d.np),now(old.size(),0xa55a);
   for(uint32_t row=0;row<d.kp;row++)for(uint32_t lane=0;lane<4;lane++){hunet_fused_pack_row(s,d,x.data(),old.data(),row,lane,4);need(!vxnn_hunet_pack_row(&s,&d,x.data(),x.size(),now.data(),now.size(),row,lane,4),"new typed PACK wraps original mathematical source");}
   need(old==now,"all frozen FP16 RNE packed elements equal old path");values+=now.size();
   auto saved=now;need(vxnn_hunet_pack_row(&s,&d,x.data(),x.size(),now.data(),now.size()-1,0,0,4)<0&&saved==now,"short destination rejected without mutation");
   if(stages==1&&di==0){auto bad=s;bad.ci=0;need(vxnn_hunet_pack_row(&bad,&d,x.data(),x.size(),now.data(),now.size(),0,0,4)<0,"zero input shape rejected");need(vxnn_hunet_pack_row(&s,&d,x.data(),x.size()-1,now.data(),now.size(),0,0,4)<0,"short input rejected");need(vxnn_hunet_pack_row(&s,&d,x.data(),x.size(),now.data(),now.size(),d.kp,0,4)<0,"row outside padded K rejected");need(vxnn_hunet_pack_row(&s,&d,x.data(),x.size(),now.data(),now.size(),0,4,4)<0,"fifth lane rejected");need(vxnn_hunet_pack_row(&s,&d,x.data(),x.size(),reinterpret_cast<uint16_t*>(x.data()),now.size(),0,0,4)<0,"read-write alias rejected before access");need(saved==now,"all rejected packs preserve output");}
  }
 }
 need(stages==35&&descriptors==314,"all frozen matrix descriptors covered");std::printf("U12_PACK_35_STAGES_314_DESCRIPTORS_%llu_VALUES_OLD_PATH_EXACT_NOT_GPU\n",(unsigned long long)values);
}
static void store(){
 HunetFusedDescriptorV1 d{};d.m=13;d.mp=16;d.n=11;d.np=8;d.k=1;d.kp=8;d.offset=3;d.count=7;std::vector<float>out(d.m*d.n+2,999),expected=out,bias(d.m);for(unsigned r=0;r<d.m;r++)bias[r]=float(r)/4;
 for(uint32_t row=0;row<d.mp;row+=8)for(uint32_t col=0;col<d.np;col+=4)for(uint32_t lane=0;lane<4;lane++){float acc[8];for(uint32_t reg=0;reg<8;reg++){auto r=row+lane/2+(reg/2)*2,c=col+lane%2+(reg%2)*2;acc[reg]=float(r*100+c);if(r<d.m&&c<d.count)expected[1+r*d.n+d.offset+c]=acc[reg]+bias[r];}need(!vxnn_hunet_store_fragment(&d,out.data()+1,d.m*d.n,bias.data(),bias.size(),row,col,lane,acc,8),"native WMMA accumulator layout epilogue");}
 need(out==expected,"all lane/register stores and padded tail preserve exact output and guards");float acc[8]={};auto saved=out;need(vxnn_hunet_store_fragment(&d,out.data()+1,d.m*d.n,bias.data(),bias.size(),0,0,4,acc,8)<0&&out==saved,"invalid fifth lane does not publish");
 need(vxnn_hunet_store_fragment(&d,out.data()+1,d.m*d.n-1,bias.data(),bias.size(),0,0,0,acc,8)<0&&out==saved,"short epilogue destination rejected");need(vxnn_hunet_store_fragment(&d,out.data()+1,d.m*d.n,out.data()+1,bias.size(),0,0,0,acc,8)<0&&out==saved,"bias-output alias rejected");need(vxnn_hunet_store_fragment(&d,out.data()+1,d.m*d.n,bias.data(),bias.size(),0,0,0,acc,7)<0&&out==saved,"short register fragment rejected");
}
static void stats(){
 for(size_t n:{size_t(1),size_t(4),size_t(5),size_t(4097)}){std::vector<float>x(n);for(size_t i=0;i<n;i++)x[i]=i%3==0?1e4f:i%3==1?-1e4f:float(i%29)/31;float mean=hunet_kahan_sum_ordered4(x.data(),n)/float(n);float reference[2]={mean,1.0f/sqrtf(hunet_variance_sum_ordered4(x.data(),n,mean)/float(n)+1e-5f)},out[2]={111,222};need(!vxnn_hunet_gn_stats(x.data(),n,1e-5f,out,2),"new GN uses original ordered Kahan chains");need(!std::memcmp(out,reference,8),"GN mean/invstd bitwise old reduction order");out[0]=111;out[1]=222;need(vxnn_hunet_gn_stats(x.data(),n,0,out,2)<0&&out[0]==111&&out[1]==222,"invalid epsilon rejected without output");}
 float x[2]={1,2},out[2]={111,222};need(vxnn_hunet_gn_stats(x,0,1e-5f,out,2)<0,"empty stats rejected");need(vxnn_hunet_gn_stats(x,2,1e-5f,x,2)<0&&x[0]==1&&x[1]==2,"stats read-write alias rejected");need(vxnn_hunet_gn_stats(x,2,NAN,out,2)<0,"nonfinite epsilon rejected");x[0]=INFINITY;need(vxnn_hunet_gn_stats(x,2,1e-5f,out,2)<0&&out[0]==111&&out[1]==222,"nonfinite statistics retained as error without output");
}
int main(){pack();store();stats();puts("U12_HUNET_MATH_COMPONENT_ONLY_CPU_GPU_DEPENDENCY_AND_FULL_MIGRATION_PENDING gpu_raw_cycles=null");}

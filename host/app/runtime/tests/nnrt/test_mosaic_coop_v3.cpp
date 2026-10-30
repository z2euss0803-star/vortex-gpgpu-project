#include "device/vxnn/mosaic_wire_coop_v3.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
static unsigned cases;
static void check(bool yes,const char *why){++cases;if(!yes){std::fprintf(stderr,"COOP_V3_ASSERT %s case=%u\n",why,cases);std::exit(71);}}
static VxnnMosaicArgs args(unsigned op,bool save=false){
 VxnnMosaicArgs a{};a.abi=3;a.bytes=128;a.opcode=op;a.flags=op>=7?unsigned(save):op>=3?2:op==1?unsigned(save):0;
 a.ci=3;a.co=5;a.h=8;a.w=12;a.k=3;a.stride=a.upsample=1;a.kpad=32;a.count=1;if(op<3||op>=7)a.scale_bits=0x3f800000;
 for(auto &n:a.extent){n=64;}
 a.extent[VXM_WORK]=1088;
 if(op==1){a.extent[VXM_INPUT]=1152;a.extent[VXM_QUANT]=576;if(save)a.extent[VXM_SKIP]=1152;}
 if(op==2){a.extent[VXM_OUTPUT]=1920;a.extent[VXM_QUANT]=576;a.extent[VXM_WEIGHTS]=320;a.extent[VXM_COEFF]=40;a.extent[VXM_WORK]=8768;}
 if(op>=3&&op<=6){a.extent[VXM_INPUT]=1920;if(op!=3)a.extent[VXM_OUTPUT]=1920;}
 if(op==3){a.extent[VXM_STATS]=60;a.extent[VXM_WORK]=5184;}
 if(op==4||op>=7){a.extent[VXM_COEFF]=80;a.extent[VXM_STATS]=60;}
 if(op>=7){a.extent[VXM_INPUT]=1920;a.extent[VXM_QUANT]=960;if(save)a.extent[VXM_OUTPUT]=1920;}
 if(op==6||op==8)a.extent[VXM_SKIP]=1920;
 return a;
}
static unsigned mask(unsigned op,bool save){
 unsigned m=1u<<VXM_WORK;
 if(op==1)return m|(1u<<VXM_INPUT)|(1u<<VXM_QUANT)|(save?(1u<<VXM_SKIP):0);
 if(op==2)return m|(1u<<VXM_OUTPUT)|(1u<<VXM_QUANT)|(1u<<VXM_WEIGHTS)|(1u<<VXM_COEFF);
 if(op==3)return m|(1u<<VXM_INPUT)|(1u<<VXM_STATS);
 if(op==4)return m|(1u<<VXM_INPUT)|(1u<<VXM_OUTPUT)|(1u<<VXM_COEFF)|(1u<<VXM_STATS);
 if(op==5)return m|(1u<<VXM_INPUT)|(1u<<VXM_OUTPUT);
 if(op==6)return m|(1u<<VXM_INPUT)|(1u<<VXM_OUTPUT)|(1u<<VXM_SKIP);
 return m|(1u<<VXM_INPUT)|(1u<<VXM_QUANT)|(1u<<VXM_COEFF)|(1u<<VXM_STATS)|(op==8?(1u<<VXM_SKIP):0)|(save?(1u<<VXM_OUTPUT):0);
}
static void place(VxnnMosaicArgs &a,unsigned used){
 uint64_t next=0x80800000u;
 for(unsigned i=0;i<8;i++)if(used&(1u<<i)){a.address[i]=uint32_t(next);next=(next+a.extent[i]+63)/64*64;}
 for(unsigned i=0;i<8;i++)if(!(used&(1u<<i)))a.address[i]=(i==VXM_INPUT||i==VXM_WEIGHTS||i==VXM_COEFF)?uint32_t(next):a.address[VXM_WORK];
}
static VxnnMosaicArgs old_packet(VxnnMosaicArgs a,uint32_t old_work){a.abi=a.opcode>=7?2:1;a.reserved=0;a.extent[VXM_WORK]=old_work;return a;}
int main(){
 VxnnMosaicShape s{};
 for(unsigned op=1;op<=8;op++)for(unsigned save=0;save<((op==1||op>=7)?2u:1u);save++)for(unsigned enabled=0;enabled<2;enabled++){
  auto a=args(op,save);a.reserved=enabled;const unsigned used=mask(op,save);
  check(!vxnn_mosaic_validate_coop_v3(&a,false,&s),"valid ABI3 all opcodes/options");
  check(s.used_mask==used&&s.required[VXM_WORK]==a.extent[VXM_WORK],"exact roles and expanded work published");
  check(s.core_work_bytes==(op==2?2176u:op==3?1280u:256u),"literal per-core stride including pointwise");
  check(vxnn_mosaic_validate_fused(&a,false,&s)<0,"old ABI validator rejects ABI3");
  for(unsigned role=0;role<8;role++)for(int delta:{-1,1}){auto b=a;b.extent[role]=uint32_t(int64_t(b.extent[role])+delta);check(vxnn_mosaic_validate_coop_v3(&b,false,&s)<0,"every extent exact plus/minus one");}
  for(unsigned abi:{0u,1u,2u,4u,0xffffffffu}){auto b=a;b.abi=abi;check(vxnn_mosaic_validate_coop_v3(&b,false,&s)<0,"only ABI3");}
  for(unsigned option:{2u,3u,0x80000000u,0xffffffffu}){auto b=a;b.reserved=option;check(vxnn_mosaic_validate_coop_v3(&b,false,&s)<0,"only options bit0");}
  auto b=a;b.bytes=124;check(vxnn_mosaic_validate_coop_v3(&b,false,&s)<0,"exact128 bytes");
  b=a;b.address[VXM_WORK]=0x81000000;check(vxnn_mosaic_validate_coop_v3(&b,false,&s)<0,"unrelocated addresses zero");
  const uint32_t old_work=op==2?6208:op==3?4160:64;
  b=a;b.extent[VXM_WORK]=old_work;check(vxnn_mosaic_validate_coop_v3(&b,false,&s)<0,"ABI3 rejects old shorter workspace");
  auto old=old_packet(a,old_work);check(!vxnn_mosaic_validate_fused(&old,false,&s),"unchanged old ABI remains accepted");
  place(a,used);check(!vxnn_mosaic_validate_coop_v3(&a,true,&s),"valid relocated all roles");
  for(unsigned i=0;i<8;i++)if(!(used&(1u<<i))&&(i==VXM_INPUT||i==VXM_WEIGHTS||i==VXM_COEFF)){
   b=a;for(unsigned j=0;j<8;j++)if(!(used&(1u<<j))&&(j==VXM_INPUT||j==VXM_WEIGHTS||j==VXM_COEFF))b.address[j]=a.address[VXM_WORK]+old_work;
   old=old_packet(b,old_work);check(!vxnn_mosaic_validate_fused(&old,true,&s),"old range alone misses added-tail readonly alias");
   check(vxnn_mosaic_validate_coop_v3(&b,true,&s)<0,"inactive readonly cannot alias expanded tail");
   b=a;b.address[i]=a.address[VXM_WORK];check(vxnn_mosaic_validate_coop_v3(&b,true,&s)<0,"inactive readonly cannot alias canonical prefix");break;
  }
 }
 auto conv=args(2);place(conv,mask(2,false));auto bad=conv;
 bad.address[VXM_COEFF]=bad.address[VXM_WORK]+6208;auto old=old_packet(bad,6208);
 check(!vxnn_mosaic_validate_fused(&old,true,&s),"legacy shadow accepts active RO beyond old WORK");
 check(vxnn_mosaic_validate_coop_v3(&bad,true,&s)<0,"active RO in expanded tail rejected independently");
 bad=conv;bad.address[VXM_QUANT]=bad.address[VXM_WORK]+6208;
 check(vxnn_mosaic_validate_coop_v3(&bad,true,&s)<0,"active read/write tensor in expanded tail rejected");
 auto edge=args(1);place(edge,mask(1,false));edge.address[VXM_WORK]=0xfff00000u-1088;
 for(unsigned i:{unsigned(VXM_OUTPUT),unsigned(VXM_STATS),unsigned(VXM_SKIP)})edge.address[i]=edge.address[VXM_WORK];
 check(!vxnn_mosaic_validate_coop_v3(&edge,true,&s),"true WORK ends exactly at address ceiling");
 edge.address[VXM_WORK]+=64;for(unsigned i:{unsigned(VXM_OUTPUT),unsigned(VXM_STATS),unsigned(VXM_SKIP)})edge.address[i]=edge.address[VXM_WORK];
 old=old_packet(edge,64);check(!vxnn_mosaic_validate_fused(&old,true,&s),"old prefix below ceiling insufficient");
 check(vxnn_mosaic_validate_coop_v3(&edge,true,&s)<0,"true expanded WORK beyond ceiling rejected");
 auto full=args(2);full.ci=3;full.co=32;full.h=960;full.w=1280;full.k=9;full.kpad=256;full.count=256;
 full.extent[VXM_OUTPUT]=157286400;full.extent[VXM_QUANT]=7372800;full.extent[VXM_WEIGHTS]=16384;full.extent[VXM_COEFF]=256;full.extent[VXM_WORK]=37440;
 check(!vxnn_mosaic_validate_coop_v3(&full,false,&s)&&s.items==614400&&s.core_work_bytes==9344,"full first layer literal cardinality and WORK");place(full,mask(2,false));check(!vxnn_mosaic_validate_coop_v3(&full,true,&s),"full first layer actual ranges");
 auto maximum=args(2);maximum.ci=32;maximum.co=3;maximum.k=9;maximum.kpad=2592;maximum.extent[VXM_OUTPUT]=1152;maximum.extent[VXM_QUANT]=6144;maximum.extent[VXM_WEIGHTS]=15552;maximum.extent[VXM_COEFF]=24;maximum.extent[VXM_WORK]=336448;
 check(!vxnn_mosaic_validate_coop_v3(&maximum,false,&s)&&s.core_work_bytes==84096,"max kpad literal WORK");
 auto norm=args(3);norm.ci=norm.co=128;norm.h=960;norm.w=1280;norm.kpad=1152;norm.extent[VXM_INPUT]=629145600;norm.extent[VXM_STATS]=1536;norm.extent[VXM_WORK]=67109952;
 check(!vxnn_mosaic_validate_coop_v3(&norm,false,&s)&&s.norm_elements==2097152&&s.core_work_bytes==16777472,"max norm literal tree and WORK");place(norm,mask(3,false));check(!vxnn_mosaic_validate_coop_v3(&norm,true,&s),"max norm actual ranges");
 for(unsigned bad_value:{0u,1u,0xffffffffu}){bad=norm;bad.h=bad_value;check(vxnn_mosaic_validate_coop_v3(&bad,false,&s)<0,"norm geometry bounded before multiply");bad=norm;bad.upsample=bad_value;check(vxnn_mosaic_validate_coop_v3(&bad,false,&s)<0,"norm multiplier bounded before derive");}
 auto minimum=args(2);minimum.ci=minimum.co=1;minimum.h=minimum.w=2;minimum.stride=2;minimum.extent[VXM_OUTPUT]=4;minimum.extent[VXM_QUANT]=8;minimum.extent[VXM_WEIGHTS]=64;minimum.extent[VXM_COEFF]=8;
 check(!vxnn_mosaic_validate_coop_v3(&minimum,false,&s)&&s.items==1&&s.core_work_bytes==2176,"literal minimum geometry convolution");
 auto minimum_norm=args(3);minimum_norm.ci=minimum_norm.co=1;minimum_norm.h=minimum_norm.w=2;minimum_norm.stride=2;minimum_norm.extent[VXM_INPUT]=4;minimum_norm.extent[VXM_STATS]=12;minimum_norm.extent[VXM_WORK]=1344;
 check(!vxnn_mosaic_validate_coop_v3(&minimum_norm,false,&s)&&s.norm_elements==1&&s.core_work_bytes==320,"literal minimum geometry norm tree1");
 for(auto item:{minimum,minimum_norm,full,maximum,norm}){
  for(int delta:{-1,1}){auto size_bad=item;size_bad.extent[VXM_WORK]=uint32_t(int64_t(item.extent[VXM_WORK])+delta);check(vxnn_mosaic_validate_coop_v3(&size_bad,false,&s)<0,"min/max/full exact WORK plus/minus one");}
  place(item,mask(item.opcode,false));check(!vxnn_mosaic_validate_coop_v3(&item,true,&s),"all min/max relocated WORK spans");
 }
 auto tail=args(2);tail.first=11;tail.count=1;check(!vxnn_mosaic_validate_coop_v3(&tail,false,&s),"last legal convolution tile accepted");tail.count=2;check(vxnn_mosaic_validate_coop_v3(&tail,false,&s)==-ERANGE,"convolution tail escape rejected");
 tail=args(7);tail.first=1;check(!vxnn_mosaic_validate_coop_v3(&tail,false,&s),"fused last item accepted");tail.first=2;check(vxnn_mosaic_validate_coop_v3(&tail,false,&s)==-ERANGE,"fused first past end rejected");
 for(unsigned count:{0u,4097u,0xffffffffu}){tail=args(2);tail.count=count;check(vxnn_mosaic_validate_coop_v3(&tail,false,&s)<0,"bounded real tile count unchanged");}
 VxnnMosaicShape untouched;std::memset(&untouched,0xa7,sizeof untouched);auto before=untouched;bad=args(1);bad.abi=2;
 check(vxnn_mosaic_validate_coop_v3(&bad,false,&untouched)<0&&!std::memcmp(&before,&untouched,sizeof before),"shape never published on rejection");
 check(vxnn_mosaic_validate_coop_v3(nullptr,false,&s)<0&&vxnn_mosaic_validate_coop_v3(&bad,false,nullptr)<0,"null pointers rejected");
 std::printf("MOSAIC_COOP_ABI3_EXACT_EXTENTS_RO64_EXPANDED_TAILS_BOUNDS_HOST_PASS checks=%u gpu_raw_cycles=null\n",cases);
}



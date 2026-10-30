#include <cassert>
#include <cstdio>
#include <vector>
#include <cstring>
#include <fstream>
#include "../../device/hunet_opt_pack.h"
#include "../../generated/hunet_opt/views.h"
#include "../../generated/hunet_opt/device_graph.h"
static void actual_windows(){
 std::ifstream f("generated/hunet_fused/descriptors.bin",std::ios::binary|std::ios::ate);assert(f);auto size=f.tellg();std::vector<HunetFusedDescriptorV1> ds(size_t(size)/64);f.seekg(0);f.read(reinterpret_cast<char*>(ds.data()),size);assert(f);
 unsigned consumers=0;
 for(const auto&block:opt_graphs){
  vxl_opt_graph_node_v2 nodes[5];std::memcpy(nodes,block.descriptor+128,sizeof(nodes));const auto&a=nodes[0];const auto&bnode=nodes[1];const auto&midnode=nodes[2];uint32_t hw=a.ih*a.iw;
  std::vector<float>root(a.ci*hw),mid(midnode.co*hw);for(unsigned i=0;i<root.size();i++)root[i]=float(int(i%257)-128)/27;for(unsigned i=0;i<mid.size();i++)mid[i]=float(int(i%127)-63)/13;
  auto read=[&](uint32_t address){if(address>=a.input0&&uint64_t(address)-a.input0<root.size()*4)return root[(address-a.input0)/4];assert(address>=midnode.output&&uint64_t(address)-midnode.output<mid.size()*4);return mid[(address-midnode.output)/4];};
  for(unsigned index:{2u,4u}){
   const auto*view=opt_bound_view(block.block_id,index);const auto*s=opt_bound_matrix(nodes[index].stage);assert(view&&s);std::vector<float> materialized(s->ci*hw);
   if(index==2){assert(s->ci==a.co);std::memcpy(materialized.data(),root.data()+a.groups*hw,materialized.size()*4);}
   else {assert(s->ci==midnode.co+bnode.co);std::memcpy(materialized.data(),mid.data(),mid.size()*4);std::memcpy(materialized.data()+mid.size(),root.data()+bnode.groups*hw,bnode.co*hw*4);}
   const auto*table=ds.data()+(s->stage_descriptors_gpu-0x86d0df80)/64;
   for(unsigned i=0;i<s->descriptor_count;i++){const auto&d=table[i];std::vector<uint16_t> old(d.kp*d.np),now(old.size());
    for(unsigned row=0;row<d.kp;row++)for(unsigned lane=0;lane<4;lane++){hunet_fused_pack_row(*s,d,materialized.data(),old.data(),row,lane,4);opt_pack_row_view(*s,d,*view,read,now.data(),row,lane,4);}
    assert(old==now);
   }consumers++;
  }
 }assert(consumers==8);puts("EIGHT_FROZEN_SHAPE_VIEW_PACK_CONSUMERS_EXACT_HOST_PASS_NOT_GPU");
}
int main(){
 OptViewSegment segments[]={{0,2,0x85000040,1,5,3,4,0,0},{2,3,0x85010040,0,3,3,4,0,0}};
 OptView view={1,2,5,3,4,2,segments};
 for(unsigned c=0;c<5;c++)for(unsigned y=0;y<3;y++)for(unsigned x=0;x<4;x++){
  auto p=opt_view_address(view,c,y,x);uint32_t base=c<2?0x85000040:0x85010040,channel=c<2?c+1:c-2;
  assert(p.valid&&p.gpu==base+((channel*3+y)*4+x)*4);
 }
 assert(!opt_view_address(view,5,0,0).valid&&!opt_view_address(view,0,3,0).valid);
 for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){
  segments[0].dy=dy;segments[0].dx=dx;
  for(unsigned y=0;y<3;y++)for(unsigned x=0;x<4;x++){auto p=opt_view_address(view,0,y,x);int yy=int(y)+dy,xx=int(x)+dx;assert(p.valid==(yy>=0&&xx>=0&&yy<3&&xx<4));if(p.valid)assert(p.gpu==0x85000040+((3+yy)*4+xx)*4);}
 }
 segments[0].dy=segments[0].dx=0;segments[0].source_gpu=UINT32_MAX-3;assert(!opt_view_address(view,0,0,0).valid);segments[0].source_gpu=0x85000040;
 /* Real source indirection used by the actual PACK view consumer. */
 HunetFusedStageV1 s={};s.ci=5;s.ih=s.oh=3;s.iw=s.ow=4;s.kernel_k=s.stride=1;
 HunetFusedDescriptorV1 d={};d.k=5;d.kp=8;d.n=d.count=d.np=12;
 std::vector<float> a(5*12),b(3*12),materialized(5*12);for(unsigned i=0;i<a.size();i++)a[i]=float(i)/17;for(unsigned i=0;i<b.size();i++)b[i]=-float(i)/31;
 for(unsigned c=0;c<5;c++)for(unsigned q=0;q<12;q++)materialized[c*12+q]=c<2?a[(c+1)*12+q]:b[(c-2)*12+q];
 auto read=[&](uint32_t gpu){return gpu>=0x85010040?b[(gpu-0x85010040)/4]:a[(gpu-0x85000040)/4];};
 std::vector<uint16_t> expected(8*12),actual(8*12);
 for(unsigned row=0;row<8;row++)for(unsigned lane=0;lane<4;lane++){hunet_fused_pack_row(s,d,materialized.data(),expected.data(),row,lane,4);opt_pack_row_view(s,d,view,read,actual.data(),row,lane,4);}
 assert(expected==actual);actual_windows();puts("VIEW_SLICE_CONCAT_SHIFT_BOUNDARIES_GPU32_AND_ACTUAL_PACK_PASS_HOST_NOT_GPU");return 0;
}

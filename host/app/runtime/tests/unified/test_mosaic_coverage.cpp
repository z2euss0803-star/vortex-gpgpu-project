#include <vxnn/mosaic_coverage.hpp>
#include <vxl/mosaic_host.h>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
using namespace vxnn;
namespace {
void need(bool v,const char*s){if(!v){std::fprintf(stderr,"U13_COVERAGE_ASSERT %s\n",s);std::exit(74);}}
template<class T>T got(Result<T>r,const char*s){if(!r)std::fprintf(stderr,"COVERAGE_ERROR %d %s\n",r.error->code,r.error->detail.c_str());need(bool(r),s);return *r.value;}
}
int main(){
 auto full=got(describe_mosaic_phase(1280,960,1),"native full-resolution first convolution metadata must exist");need(full.width==1280&&full.height==960&&full.layer==0&&full.opcode==2&&full.items==614400,"hand counted ceil32/8 * ceil1280*960/8 first layer");auto q=got(describe_mosaic_phase(1280,960,0),"first quantize");need(q.opcode==1&&q.items==14400,"hand counted3*1280*960/256");auto st=got(describe_mosaic_phase(1280,960,2),"first statistics");need(st.opcode==3&&st.items==32,"one channel per statistics item");auto last=got(describe_mosaic_phase(1280,960,76),"last convolution");need(last.layer==15&&last.opcode==2&&last.items==153600,"last3 padded to8 output channels");
 uint64_t batches=0,items=0;
 for(const auto&size:std::array<std::array<uint32_t,2>,5>{{{{8,8}},{{12,8}},{{16,20}},{{256,144}},{{1280,960}}}}){
  for(uint32_t phase=0;phase<77;phase++){
   uint32_t old_layer=0,old_op=0,old_items=0;need(!mosaic_phase(phase,size[0],size[1],&old_layer,&old_op,&old_items),"independent frozen C model phase");auto p=got(describe_mosaic_phase(size[0],size[1],phase),"all frozen phases");need(p.layer==old_layer&&p.opcode==old_op&&p.items==old_items,"all77 geometry/counts match frozen model; no driver rules changed");
   for(uint32_t budget:{1u,3u,4u,16u,64u,256u,4095u,4096u}){
    auto plan=got(bounded_tiles(p,budget),"all old legal item budgets, including nonmultiple4");need(plan.jobs()==(uint64_t(old_items)+budget-1)/budget&&plan.phase().index==phase&&plan.budget()==budget,"bounded lazy job count");uint64_t seen=0;std::array<uint64_t,4>seen_core{};
    for(uint64_t i=0;i<plan.jobs();i++){auto b=got(plan.at(i),"legal batch");need(b.first==seen&&b.count&&b.count<=budget&&b.count<=old_items-b.first,"no gap, overlap, zero batch or tail escape");std::array<uint32_t,4>expected{};for(uint32_t k=0;k<b.count;k++)++expected[(b.first+k)%4];need(b.core_items==expected,"four-core assignment uses GLOBAL item modulo, no per-batch rotation/reset");for(unsigned k=0;k<4;k++)seen_core[k]+=b.core_items[k];seen+=b.count;++batches;}
    need(seen==old_items,"exact complete coverage");for(unsigned core=0;core<4;core++)need(seen_core[core]==(uint64_t(old_items)+3-core)/4,"each item assigned exactly once to original owner core");need(!plan.at(plan.jobs())&&!plan.at(UINT64_MAX),"end and huge index rejected without multiplication overflow");items+=seen;
   }
   auto bad=p;++bad.items;need(!bounded_tiles(bad,4),"tampered extent rejected");bad=p;bad.opcode=7;need(!bounded_tiles(bad,4),"unknown operator");need(!bounded_tiles(p,0)&&!bounded_tiles(p,4097),"old4096 bound not raised");
  }
  need(!describe_mosaic_phase(size[0],size[1],77),"no78th phase");
 }
 for(const auto&size:std::array<std::array<uint32_t,2>,5>{{{{0,8}},{{7,8}},{{1284,960}},{{1280,964}},{{10,8}}}})need(!describe_mosaic_phase(size[0],size[1],0),"invalid geometry rejected");
 std::printf("U13_MOSAIC_LAZY_COVERAGE_77_PHASES_5_GEOMETRIES_8_BUDGETS_PASS_HOST batches=%llu items=%llu gpu_raw_cycles=null\n",(unsigned long long)batches,(unsigned long long)items);
}

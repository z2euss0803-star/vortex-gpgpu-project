// Compile the actual entry, replacing the unavailable GPU instructions and
// observing stores without changing their FP32 expressions or output values.
// This catches a reversion to one-element dispatch, missing/twice-covered tails,
// changed FP32 order, skipped finite checks, and lost RNE admission checks.
#include "device/vxnn/hunet_wire.h"
#include "vendor/hunet/v5/include/hunet_ops_baseline.h"
#include <sys/mman.h>
#include <cfenv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <type_traits>
#include <vector>

static void need(bool ok,const char*why){if(!ok){std::fprintf(stderr,"R4_HUNET_ASSERT %s\n",why);std::exit(69);}}
struct Dim { uint32_t x; };
static Dim blockIdx{},threadIdx{},blockDim{};
static uint32_t r4_errors[4],r4_grid,r4_calls;
static bool r4_change_rounding;
static float*r4_output;
static std::vector<unsigned> r4_writes;
static void r4_track_write(float*y,uint32_t t){
 need(y==r4_output&&t<r4_writes.size(),"span store stays inside the validated output");++r4_writes[t];
}
static uint32_t vx_core_id(){return 0;}
static uint32_t vx_num_cores(){return 4;}
static uint32_t vx_num_warps(){return 4;}
static uint32_t vx_num_threads(){return 4;}
static void vx_barrier(uint32_t,uint32_t){}
enum { VX_CSR_MSCRATCH=0 };
static uint32_t csr_read(uint32_t){return 0x80100040;}
static int vx_spawn_threads(uint32_t dims,const uint32_t*grid,const uint32_t*block,void(*body)(void*),void*arg){
 need(dims==1&&*block==4,"real entry keeps four-lane launch ABI");r4_grid=*grid;blockDim.x=*block;
 for(blockIdx.x=0;blockIdx.x<*grid;++blockIdx.x)for(threadIdx.x=0;threadIdx.x<*block;++threadIdx.x){
  ++r4_calls;body(arg);if(r4_change_rounding&&r4_calls==1)need(!std::fesetround(FE_UPWARD),"inject rounding change");
 }
 return 0;
}
// Fused GPU code is compiled but is never executed in this scalar-only test.
namespace vortex { namespace tensor {
struct fp16{};struct fp32{};
template<int,class,class>struct wmma_context {
 enum { tileM=8,tileN=4,tileK=8 };
 struct fragment_a{};struct fragment_b{};
 struct fragment_acc { enum { NR=8 };float data[8]; };
 static void fill_fragment(fragment_acc&acc,float value){for(float&v:acc.data)v=value;}
 template<class T>static void load_matrix_sync(T&,const uint16_t*,uint32_t){}
 static void mma_sync(fragment_acc&,const fragment_a&,const fragment_b&,const fragment_acc&){}
};
namespace detail {
template<int N,class F>void unroll_for(F f){if constexpr(N>0){unroll_for<N-1>(f);f(std::integral_constant<int,N-1>{});}}
}
}}
#include "r4_hunet_entry_host.inc"

struct Mapping {
 void*p;size_t n;
 Mapping(uintptr_t address,size_t bytes):p(mmap(reinterpret_cast<void*>(address),bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)),n(bytes){
  need(p!=MAP_FAILED&&reinterpret_cast<uintptr_t>(p)==address,"disjoint legacy GPU32 test arena");
 }
 ~Mapping(){munmap(p,n);}
};
static size_t next;
static float*allocate(size_t n){next=(next+63)&~size_t(63);need(next+n*4<64*1024*1024,"test arena capacity");float*p=reinterpret_cast<float*>(0x81000000+next);next+=n*4;return p;}
static uint32_t address(const void*p){return uint32_t(reinterpret_cast<uintptr_t>(p));}
static bool span_op(uint32_t op){return op==HUNET_GN_APPLY||op==HUNET_ADD||op==HUNET_MUL_CHANNEL||op==HUNET_COPY||op==HUNET_SLICE||op==HUNET_CONCAT||op==HUNET_SHIFT;}
static uint64_t values,jobs,launches;
static HunetJobV1 job(uint32_t op,uint32_t ci,uint32_t co,uint32_t h,uint32_t w){
 HunetJobV1 j{};j.magic=HUNET_MAGIC;j.abi=1;j.bytes=128;j.dtype=1;j.op=op;j.ci=ci;j.co=co;j.ih=j.oh=h;j.iw=j.ow=w;
 if(op==HUNET_GN_STATS){j.tasks=1;j.epsilon_bits=hunet_bits(1e-5f);}else if(op==HUNET_GAP){j.oh=j.ow=1;j.tasks=co;}else j.tasks=co*h*w;
 j.nitems=op==HUNET_GN_STATS?ci*h*w:co*j.oh*j.ow;if(op==HUNET_SHIFT)j.n_div=ci>=5?5:1;if(op==HUNET_SLICE)j.groups=2;return j;
}
struct Fixture {
 HunetJobV1 j,legacy;VxnnHunetArgs a{};float*x,*y,*old;size_t ny;
 explicit Fixture(HunetJobV1 input):j(input),legacy(input){
  next=0;size_t nx=size_t(j.ci)*j.ih*j.iw;ny=j.op==HUNET_GN_STATS?2:size_t(j.co)*j.oh*j.ow;
  size_t nw=j.op==HUNET_ADD?ny:j.op==HUNET_MUL_CHANNEL?j.co:j.op==HUNET_CONCAT?ny-nx:0;
  x=allocate(nx);float*w=allocate(nw?nw:1),*g=allocate(j.ci),*b=allocate(j.ci),*s=allocate(2);
  y=allocate(ny+2)+1;old=allocate(ny+2)+1;float*work=allocate(4);
  for(size_t i=0;i<nx;i++){const float v[]={-0.0f,1e4f,-1e4f,.000000059604644775390625f,-.000000059604644775390625f,.125f};x[i]=i%13<6?v[i%13]:float(int(i%139)-69)/31;}
  for(size_t i=0;i<nw;i++)w[i]=i%7==0?-0.0f:float(int(i%71)-35)/29;
  for(uint32_t i=0;i<j.ci;i++){g[i]=float(int(i%7)-3)/9;b[i]=float(int(i%13)-6)/11;}
  s[0]=.1234567f;s[1]=.7654321f;
  for(size_t i=0;i<ny+2;i++){(y-1)[i]=hunet_float(0x7fc12345u);(old-1)[i]=hunet_float(0x7fc12345u);}
  legacy.x=address(x);legacy.w=address(w);legacy.gamma=address(g);legacy.beta=address(b);legacy.scratch=address(s);legacy.y=address(old);
  a.abi=1;a.bytes=128;a.op=j.op;a.ci=j.ci;a.co=j.co;a.ih=j.ih;a.iw=j.iw;a.oh=j.oh;a.ow=j.ow;a.groups=j.groups;a.n_div=j.n_div;a.epsilon_bits=j.epsilon_bits;
  for(unsigned i=0;i<8;i++){a.address[i]=address(x);a.extent[i]=uint32_t(nx*4);}
  if(nw){a.address[VXH_W]=address(w);a.extent[VXH_W]=uint32_t(nw*4);}
  if(j.op==HUNET_GN_APPLY){a.address[VXH_GAMMA]=address(g);a.address[VXH_BETA]=address(b);a.extent[VXH_GAMMA]=a.extent[VXH_BETA]=j.ci*4;a.address[VXH_STATS]=address(s);a.extent[VXH_STATS]=8;}
  a.address[VXH_Y]=address(y);a.extent[VXH_Y]=uint32_t(ny*4);a.address[VXH_WORK]=address(work);a.extent[VXH_WORK]=16;
 }
 int execute(){
  std::memcpy(reinterpret_cast<void*>(uintptr_t(0x80100040)),&a,sizeof(a));std::memset(r4_errors,0,sizeof(r4_errors));r4_calls=r4_grid=0;r4_output=y;r4_writes.assign(ny,0);return r4_hunet_entry_main();
 }
 void compare(bool dispatch){
  for(uint32_t t=0;t<j.tasks;t++)hunet_scalar_baseline(&legacy,t);
  need(!execute(),"real controlled entry natural success");
  need(!std::memcmp(y-1,old-1,(ny+2)*4),"bitwise scalar-reference outputs, negative zero, and both guards");
  if(dispatch){
   const uint32_t width=span_op(j.op)&&j.tasks>=2048?128:4,want=(j.tasks+width-1)/width;
   if(r4_grid!=want){std::fprintf(stderr,"R4_DISPATCH_RED op=%u tasks=%u expected_grid=%u actual_grid=%u callbacks=%u\n",j.op,j.tasks,want,r4_grid,r4_calls);need(false,"large scalar work must use restored span32 dispatch");}
   need(r4_calls==want*4,"bounded four-lane callback cardinality");
   if(span_op(j.op))for(unsigned count:r4_writes)need(count==1,"every span output is stored exactly once, including lane/row/channel tails");
  }
  jobs++;values+=ny;launches+=r4_calls;
 }
};
static void corpus(bool dispatch){
 std::ifstream f("generated/hunet_fused/jobs.bin",std::ios::binary);need(bool(f),"frozen original schedule open");HunetJobV1 j;unsigned frozen=0;
 while(f.read(reinterpret_cast<char*>(&j),sizeof(j))){if(j.op==15)continue;Fixture(j).compare(dispatch);frozen++;}
 need(f.eof()&&f.gcount()==0&&frozen==68,"all 68 frozen scalar jobs read completely");
 for(uint32_t op:{HUNET_GN_APPLY,HUNET_ADD,HUNET_MUL_CHANNEL,HUNET_COPY,HUNET_SHIFT,HUNET_SLICE,HUNET_CONCAT}){
  for(uint32_t n:{1u,3u,4u,5u,127u,128u,129u,2047u,2048u,2049u,2177u}){
   if(op==HUNET_CONCAT)Fixture(job(op,1,3,1,n)).compare(dispatch);
   else Fixture(job(op,op==HUNET_SLICE?3:1,1,1,n)).compare(dispatch);
  }
  Fixture(job(op,op==HUNET_SLICE?13:op==HUNET_CONCAT?3:9,9,3,79)).compare(dispatch);
 }
 for(uint32_t n:{1u,3u,4u,5u,127u,128u,129u,2047u,2048u,2049u}){
  Fixture(job(HUNET_GN_STATS,1,1,1,n)).compare(dispatch);Fixture(job(HUNET_GAP,5,5,1,n)).compare(dispatch);
 }
 Fixture(job(HUNET_GN_STATS,1,1,17,241)).compare(dispatch);Fixture(job(HUNET_GAP,5,5,17,241)).compare(dispatch);
}
static void faults(){
 for(uint32_t op:{HUNET_GN_APPLY,HUNET_ADD,HUNET_MUL_CHANNEL,HUNET_COPY,HUNET_SHIFT,HUNET_SLICE,HUNET_CONCAT,HUNET_GN_STATS,HUNET_GAP}){
  for(float invalid:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()})for(uint32_t pos:{0u,128u,2176u}){
   Fixture c(job(op,op==HUNET_SLICE?3:op==HUNET_SHIFT?5:1,op==HUNET_CONCAT?3:op==HUNET_SHIFT?5:1,1,2177));
   const uint32_t at=op==HUNET_SLICE?c.j.groups*2177+pos:op==HUNET_SHIFT?4*2177+pos:pos;c.x[at]=invalid;
   need(c.execute()!=0,"nonfinite result anywhere in a span or reduction fails the entry");
   need(hunet_bits(c.y[-1])==0x7fc12345u&&hunet_bits(c.y[c.ny])==0x7fc12345u,"failure preserves output guards");
  }
 }
 {Fixture c(job(HUNET_ADD,1,1,1,2049));c.x[2048]=std::numeric_limits<float>::max();reinterpret_cast<float*>(uintptr_t(c.legacy.w))[2048]=std::numeric_limits<float>::max();need(c.execute()!=0,"finite inputs that overflow the last output fail the entry");}
 {Fixture c(job(HUNET_COPY,1,1,1,2049));need(!std::fesetround(FE_UPWARD),"set non-RNE mode");need(c.execute()!=0&&r4_calls==0,"reject non-RNE before dispatch");need(!std::fesetround(FE_TONEAREST),"restore RNE");}
 {Fixture c(job(HUNET_COPY,1,1,1,2049));r4_change_rounding=true;need(c.execute()!=0,"rounding change during launch is retained as error");r4_change_rounding=false;need(!std::fesetround(FE_TONEAREST),"restore RNE after injected change");}
 {Fixture c(job(HUNET_COPY,1,1,1,2049));c.a.extent[VXH_X]-=4;need(c.execute()!=0&&r4_calls==0,"short input rejected before any callback");}
 {Fixture c(job(HUNET_COPY,1,1,1,2049));c.a.address[VXH_Y]=c.a.address[VXH_X];need(c.execute()!=0&&r4_calls==0,"read-write alias rejected before any callback");}
 std::puts("R4_HUNET_FINITE_RNE_SHORT_ALIAS_FAULTS_PASS_NOT_GPU");
}
int main(int argc,char**argv){
 need(argc==2,"one test mode");Mapping args(0x80100000,4096),data(0x81000000,64*1024*1024);need(!std::fesetround(FE_TONEAREST),"RNE test admission");
 if(!std::strcmp(argv[1],"faults"))faults();else corpus(!std::strcmp(argv[1],"dispatch"));
 std::printf("R4_HUNET_%s_%llu_JOBS_%llu_VALUES_%llu_CALLBACKS_BITWISE_LEGACY_NOT_GPU gpu_raw_cycles=null\n",argv[1],(unsigned long long)jobs,(unsigned long long)values,(unsigned long long)launches);
}

#include <vxunified/p8.hpp>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <cstring>
#include <thread>
#include <atomic>
#include <type_traits>
#include <utility>
using namespace vxunified;
static void check(bool b,const char*s){if(!b){std::fprintf(stderr,"U16_FRAME_ASSERT %s\n",s);std::exit(1);}}
static std::vector<uint8_t>read(const std::string&p){std::ifstream f(p,std::ios::binary);check(bool(f),"frozen oracle readable");return {std::istreambuf_iterator<char>(f),{}};}
template<class A,class=void>struct DeferredProbe{static void run(const A&){check(false,"DEFERRED_DEVICE_PREPARATION_MISSING");}};
template<class A>struct DeferredProbe<A,std::void_t<decltype(std::declval<const A&>().prepare_for_device(std::declval<const SceneState&>(),std::shared_ptr<const TexturePackage>{},false))>>{
 static void run(const A&assets){for(unsigned object=0;object<3;++object){SceneState s;s.object=P8Object(object);s.startup_remaining=0;s.remaining=1;s.gallery=true;auto deferred=assets.prepare_for_device(s,{},false),canonical=assets.prepare(s,false);check(bool(deferred)&&bool(canonical),"deferred and canonical preparation");auto d=*deferred.value;const auto&c=*canonical.value;check(d.input_sha==c.input_sha&&d.matrix==c.matrix&&!d.cpu_reference&&!d.active_refs,"same GPU input identity and no speculative bins");for(unsigned i=0;i<10;++i)check(d.buffers[i].size()==c.buffers[i].size(),"same full allocation layout");for(unsigned i:{2u,3u,5u,9u})check(d.buffers[i]==c.buffers[i],"unchanged immutable sources");for(unsigned i:{4u,6u,7u,8u})check(std::all_of(d.buffers[i].begin(),d.buffers[i].end(),[](uint32_t v){return v==0;}),"deferred derived storage not populated by CPU reference");auto words=c.buffers[4];check(bool(assets.rebin(d,{reinterpret_cast<const uint8_t*>(words.data()),size_t(c.geometry.active_slots)*32},true)),"actual active transform readback independently rebinned");for(unsigned i:{4u,6u,7u,8u})check(d.buffers[i]==c.buffers[i],"same canonical post-transform derived data");check(d.active_refs==c.active_refs,"same active ref count");words[c.geometry.active_slots*8-1]^=1;auto old=d.buffers[8];check(!assets.rebin(d,{reinterpret_cast<const uint8_t*>(words.data()),size_t(c.geometry.active_slots)*32},true)&&d.buffers[8]==old,"bad active tail rejected before derived commit");}}
};
int main(int argc,char**argv){
 check(argc==2,"input directory");auto bank=P8Assets::load(argv[1]);check(bool(bank),"frozen frame assets must load");DeferredProbe<P8Assets>::run(**bank.value);
 const uint32_t vertices[3]={398,401,16056},faces[3]={768,648,30926};
 for(unsigned i=0;i<3;i++){auto g=(*bank.value)->geometry(P8Object(i));check(bool(g),"geometry descriptor");check(g.value->vertices==vertices[i]&&g.value->faces==faces[i]&&g.value->active_slots==(i==2?16064u:448u)&&g.value->width==(i==2?512u:256u)&&g.value->texture_width==(i==2?2048u:512u),"frozen three mesh geometry native sizes");}
 SceneState s;auto frame=(*bank.value)->prepare(s,true);check(bool(frame),"frozen porcelain pose0 prepare");
 const char*names[3]={"projected","color","depth"};const unsigned slots[3]={4,0,1};
 for(unsigned i=0;i<3;i++){auto expected=read(std::string(argv[1])+"/oracle-"+names[i]+".bin");auto&got=frame.value->buffers[slots[i]];check(expected.size()<=got.size()*4&&!std::memcmp(expected.data(),got.data(),expected.size()),"independent frozen pose0 oracle exact bytes");}
 auto f=*frame.value;auto before=f.buffers[6];check(bool((*bank.value)->rebin(f,{reinterpret_cast<const uint8_t*>(f.buffers[4].data()),f.buffers[4].size()*4})),"actual transform readback rebucketed");check(before==f.buffers[6]&&f.active_refs>0&&f.active_refs<=262144,"canonical directory stable bounded");
 auto invalid=f.buffers[4];invalid[0]=0xffffffffu;check(!(*bank.value)->rebin(f,{reinterpret_cast<const uint8_t*>(invalid.data()),invalid.size()*4}),"untrusted divergent projected readback rejected before START");
 const uint32_t poses[8]={0,100,25,101,50,102,75,103};
 for(unsigned obj=1;obj<3;obj++)for(unsigned k=0;k<(obj==2?8u:1u);k++){s.object=P8Object(obj);s.pose=poses[k];s.startup_remaining=0;s.remaining=1;s.gallery=true;auto p=(*bank.value)->prepare(s,true);check(bool(p)&&p.value->active_refs<=262144,"three meshes and eight warrior frames portable reference");for(auto color:p.value->buffers[0])check((color>>24)==255,"opaque frozen output");}
 s.pose=104;check(!(*bank.value)->prepare(s),"pose capacity rejected");s.pose=0;s.object=P8Object(3);check(!(*bank.value)->prepare(s),"invalid asset rejected");
 std::array<std::vector<uint32_t>,4>expected;for(unsigned i=0;i<4;i++){SceneState view;view.pose=25*i;auto p=(*bank.value)->prepare(view,false);check(bool(p),"single thread canonical bins");expected[i]=p.value->buffers[8];}
 std::atomic<bool>okay{true};std::vector<std::thread>workers;for(unsigned i=0;i<4;i++)workers.emplace_back([&,i](){for(unsigned repeat=0;repeat<3;repeat++){SceneState view;view.pose=25*i;auto p=(*bank.value)->prepare(view,false);if(!p||p.value->buffers[8]!=expected[i])okay=false;}});for(auto&t:workers)t.join();check(okay.load(),"four concurrent independent math contexts preserve canonical bins");
 puts("U16_FROZEN_P8_FRAME_PREPARE_PASS_CPU_ONLY_NOT_GPU_DP gpu_raw_cycles=null");
}

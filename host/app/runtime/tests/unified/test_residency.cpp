#include <vxunified/residency.hpp>
#include "scheduler_fixture.hpp"
#include <vxl/v2/program.h>
#include <vxl/hunet.h>
#include <fstream>
#include <cstring>
#include <thread>
#include <atomic>
using namespace vxunified;
namespace {
BackendIdentity hw(){BackendIdentity b;b.hardware_contract.fill(1);b.bit.fill(2);b.xsa.fill(3);b.compatible.fill(4);b.provider_flags=1;b.device_generation=7;return b;}
struct Backend:UnifiedBackend {
 BackendIdentity h=hw();int loads=0,frees=0;bool load_error=false,free_error=false,wrong_epoch=false;Digest requested{};
 Result<BackendIdentity>identity()override{return Result<BackendIdentity>::success(h);}
 Result<DeviceCaps>capabilities()override{DeviceCaps c;c.contexts=1;c.max_programs=16;return Result<DeviceCaps>::success(c);}
 Result<ProgramRef>program_load(const Digest&d)override{++loads;requested=d;if(load_error)return Result<ProgramRef>::failure({EIO,"TEST","original ambiguous load"});ProgramRef p;p.handle=loads;p.program_generation=100+loads;p.device_generation=wrong_epoch?8:7;p.program_sha=d;return Result<ProgramRef>::success(p);}
 Result<void>release_program(ProgramRef)override{++frees;return free_error?Result<void>::failure({EBUSY,"TEST","kernel still pinned"}):Result<void>::success();}
 Result<BufferRef>allocate(uint64_t,vxnn::AddressDomain,uint32_t)override{std::abort();}
 Result<void>copy(BufferRef,vxnn::Bytes)override{std::abort();}Result<void>copy(BufferRef,vxnn::MutableBytes)override{std::abort();}Result<void>copy(BufferRef,BufferRef)override{std::abort();}
 Result<void>release_buffer(BufferRef)override{std::abort();}Result<uint64_t>submit(const JobDesc&)override{std::abort();}Result<void>wait(uint64_t,uint64_t)override{std::abort();}Result<Completion>read_completion(uint64_t)override{std::abort();}Result<void>ack_reap(uint64_t)override{std::abort();}
};
}
int main(int argc,char**argv){
 require(argc==2,"catalog fixture");std::ifstream f(argv[1],std::ios::binary|std::ios::ate);auto n=f.tellg();require(n>=64&&n<=65536,"bounded catalog");std::vector<uint8_t>raw(size_t(n),0);f.seekg(0);f.read(reinterpret_cast<char*>(raw.data()),n);require(bool(f),"complete catalog");Digest sum{};vxl_hunet_hash(raw.data(),raw.size(),sum.data());auto c=value(InstalledCatalog::decode({raw.data(),raw.size()},hw(),sum),"real C-validated two-family catalog");
 ProgramKey a,b;for(unsigned i=0;i<2;i++){vxl_v2_catalog_entry e{};memcpy(&e,raw.data()+64+512*i,512);auto&k=i?b:a;k.namespace_id=e.namespace_id;k.logical_profile=e.logical_profile;k.logical_program=e.logical_program;memcpy(k.code.data(),e.code,32);memcpy(k.schema.data(),e.schema,32);memcpy(k.hardware_contract.data(),e.hardware_contract,32);}
 auto backend=std::make_shared<Backend>();auto pool=value(ProgramResidency::create(c,backend),"residency create");RequestKey one{"boot","client","one"},two{"boot","client","two"};require(backend->loads==0&&value(pool->ready(one,a),"initial readiness"),"admission never loads");auto first=value(pool->acquire(one,a),"first load");require(backend->loads==1&&first.program.program_sha==value(c->resolve(a),"binding").entry_id&&backend->requested!=a.code,"load actual entry ID only");require(!value(pool->ready(two,a),"same-family backpressure")&&!value(pool->ready(two,b),"cross-family backpressure"),"one live DATA owner even for same code");auto blocked=pool->acquire(two,b);require(!blocked&&blocked.error->code==EAGAIN&&backend->loads==1&&backend->frees==0,"no speculative eviction during live request");auto repeated=value(pool->acquire(one,a),"same owner idempotence");require(repeated.serial==first.serial&&backend->loads==1,"no duplicated program users");require(!pool->release(first,false)&&!pool->shutdown()&&backend->frees==0,"no release before DATA/consumers retire");auto stale=first;++stale.serial;require(!pool->release(stale,true),"stale lease denied");ok(pool->release(first,true),"device DATA retired");require(!pool->release(first,true),"double retirement denied");auto second=value(pool->acquire(two,a),"reuse same program");require(second.serial!=first.serial&&backend->loads==1&&backend->frees==0,"reuse resident program without load");ok(pool->release(second,true),"same-family owner done");auto other=value(pool->acquire(one,b),"switch only after retirement");require(backend->loads==2&&backend->frees==1&&other.program.program_sha!=first.program.program_sha,"old fixed reservation removed before new load");ok(pool->release(other,true),"other done");
 std::atomic<int>errors{0};std::vector<std::thread>readers;for(int i=0;i<8;i++)readers.emplace_back([&]{for(int j=0;j<1000;j++){auto r=pool->ready(two,a);if(!r||!*r.value)++errors;}});for(auto&t:readers)t.join();require(errors==0&&backend->loads==2&&backend->frees==1,"readiness is synchronized metadata, never backend I/O");ok(pool->shutdown(),"explicit cached program release");require(backend->frees==2&&!pool->ready(one,a),"closed manager cannot resurrect");
 for(int mode=0;mode<3;mode++){auto x=std::make_shared<Backend>();auto p=value(ProgramResidency::create(c,x),"fault owner");if(mode==0)x->load_error=true;if(mode==1)x->wrong_epoch=true;if(mode<2){auto fail=p->acquire(one,a);require(!fail&&p->stats().faulted,"load uncertainty quarantines resident manager");require(!p->acquire(one,a)&&x->loads==1&&x->frees==0&&!p->shutdown(),"no load retry or uncertain release");}else{auto l=value(p->acquire(one,a),"eviction fault initial");ok(p->release(l,true),"retired before eviction error");x->free_error=true;auto fail=p->acquire(two,b);require(!fail&&fail.error->code==EBUSY&&x->loads==1&&x->frees==1&&p->stats().faulted,"failed eviction retains old program; never load replacement");require(!p->acquire(two,b)&&x->frees==1,"no automatic eviction retry");}}
 auto wrong=std::make_shared<Backend>();wrong->h.xsa[0]^=1;require(!ProgramResidency::create(c,wrong),"cached backend/catalog complete identity mismatch");
 puts("U12_RESIDENCY_SINGLE_OWNER_CACHE_EVICTION_AND_FAULT_RETENTION_PASS_HOST_NO_GPU gpu_raw_cycles=null");
}

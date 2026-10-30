#include <vxunified/p8_session.hpp>
#include <cassert>
#include <cstdio>
#include <map>
using namespace vxunified;
struct Backend:UnifiedBackend {
 BackendIdentity hw;unsigned allocs=0,frees=0,copies=0,forbidden=0;bool fast=true;std::map<uint32_t,uint64_t>live;
 Backend(){hw.hardware_contract[0]=1;hw.bit[0]=2;hw.xsa[0]=3;hw.compatible[0]=4;hw.device_generation=7;hw.provider_flags=4;}
 bool fast_runtime()const override{return fast;}
 Result<BackendIdentity>identity()override{return Result<BackendIdentity>::success(hw);}
 Result<DeviceCaps>capabilities()override{++forbidden;return Result<DeviceCaps>::failure({ENOSYS});}
 Result<BufferRef>allocate(uint64_t n,vxnn::AddressDomain d,uint32_t a)override{assert(d==vxnn::AddressDomain::PL&&a==3);uint32_t id=++allocs;live[id]=n;return Result<BufferRef>::success({id,1,0,n,3,hw.device_generation});}
 Result<void>release_buffer(BufferRef r)override{assert(live.count(r.handle)&&live[r.handle]==r.bytes);live.erase(r.handle);++frees;return Result<void>::success();}
 Result<void>copy(BufferRef,vxnn::Bytes)override{++copies;return Result<void>::success();}
 Result<void>copy(BufferRef,vxnn::MutableBytes)override{++copies;return Result<void>::success();}
 Result<void>copy(BufferRef,BufferRef)override{++forbidden;return Result<void>::failure({ENOSYS});}
 Result<ProgramRef>program_load(const Digest&)override{++forbidden;return Result<ProgramRef>::failure({ENOSYS});}
 Result<void>release_program(ProgramRef)override{++forbidden;return Result<void>::failure({ENOSYS});}
 Result<uint64_t>submit(const JobDesc&)override{++forbidden;return Result<uint64_t>::failure({ENOSYS});}
 Result<void>wait(uint64_t,uint64_t)override{++forbidden;return Result<void>::failure({ENOSYS});}
 Result<Completion>read_completion(uint64_t)override{++forbidden;return Result<Completion>::failure({ENOSYS});}
 Result<void>ack_reap(uint64_t)override{++forbidden;return Result<void>::failure({ENOSYS});}
};
static P8SessionProof proof(){return {2,true,true,true,true,true};}
int main(){auto b=std::make_shared<Backend>();P8FrameSession session(b,b->hw);P8SessionKey key;key.total_bytes=4096;key.mode_sequence=2;key.layout[0]=1;key.statics[0]=2;RequestKey one{"boot","client","one"},two{"boot","client","two"};auto first=session.acquire(one,key);if(!first){std::puts("EXPECTED_RED_P8_SESSION_IMPLEMENTATION_PENDING");return 73;}assert(first.value->serial&&!first.value->root_reused&&!first.value->static_reuse&&b->allocs==1);assert(!*session.ready(two).value&&!session.acquire(two,key)&&!session.stats().fault);assert(session.initialized(first.value->serial));assert(session.retire(first.value->serial,proof()));assert(!session.stats().busy);
 auto second=session.acquire(two,key);assert(second&&second.value->root_reused&&second.value->static_reuse&&b->allocs==1);assert(session.initialized(second.value->serial)&&session.retire(second.value->serial,proof()));
 key.mode_sequence++;auto switched=session.acquire(one,key);assert(switched&&switched.value->root_reused&&!switched.value->static_reuse);assert(session.initialized(switched.value->serial)&&session.retire(switched.value->serial,proof()));key.statics[0]++;auto texture=session.acquire(two,key);assert(texture&&!texture.value->static_reuse);assert(session.initialized(texture.value->serial)&&session.retire(texture.value->serial,proof()));key.layout[0]++;auto geometry=session.acquire(one,key);assert(geometry&&!geometry.value->static_reuse);assert(session.initialized(geometry.value->serial)&&session.retire(geometry.value->serial,proof()));key.total_bytes=8192;auto size=session.acquire(two,key);assert(size&&!size.value->root_reused&&b->allocs==2&&b->frees==1);assert(session.initialized(size.value->serial));assert(session.discard(size.value->serial,true)&&b->frees==2&&!session.stats().busy);assert(session.close()&&b->live.empty()&&!b->forbidden&&!b->copies);
 for(unsigned bad=0;bad<6;bad++){auto io=std::make_shared<Backend>();P8FrameSession q(io,io->hw);auto l=q.acquire(one,key);assert(l&&q.initialized(l.value->serial));auto p=proof();switch(bad){case 0:p.completed_gpu=1;break;case 1:p.acknowledged=false;break;case 2:p.guards=false;break;case 3:p.durable_output=false;break;case 4:p.presented_retired=false;break;default:p.program_released=false;break;}assert(!q.retire(l.value->serial,p)&&q.stats().fault&&!q.acquire(two,key)&&!q.close()&&io->frees==0&&io->live.size()==1);}
 {auto io=std::make_shared<Backend>();P8FrameSession q(io,io->hw);auto l=q.acquire(one,key);assert(l&&q.initialized(l.value->serial)&&q.retire(l.value->serial,proof()));io->hw.device_generation++;assert(!q.acquire(two,key)&&q.stats().fault&&io->frees==0&&!q.close());}
 {auto io=std::make_shared<Backend>();P8FrameSession q(io,io->hw);auto l=q.acquire(one,key);assert(l);assert(!q.poison(l.value->serial,{EIO,"TEST","upload failed"}));assert(!q.ready(two)&&!q.acquire(two,key)&&!q.close()&&io->frees==0);}
 {auto io=std::make_shared<Backend>();io->fast=false;P8FrameSession q(io,io->hw);assert(!q.acquire(one,key)&&!io->allocs);}
 std::puts("P8_SESSION_INFLIGHT_BLOCK_MODE_GEOMETRY_TEXTURE_REINIT_EPOCH_STICKY_FAULT_RETIRE_PROOF_CANCEL_PASS_HOST_NOT_GPU");}

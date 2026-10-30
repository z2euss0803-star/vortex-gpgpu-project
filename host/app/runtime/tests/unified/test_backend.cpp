#include <vxunified/backend.hpp>
#include "backend_bridge.h"
#include <cassert>
#include <iostream>
using namespace vxunified;
class Transport:public IoctlTransport {public:void *bridge;explicit Transport(const char *p):bridge(backend_bridge_create(p)){}~Transport(){backend_bridge_destroy(bridge);}int call(unsigned long c,void *p) override{return backend_bridge_call(bridge,c,p);}uint64_t now_ns() override{return 100;}};
int main(int argc,char **argv){assert(argc==2);Transport io(argv[1]);ComputeV2Backend backend(io);
 auto caps=backend.capabilities();assert(caps && caps.value->max_refs_per_job==8 && !caps.value->preempt);auto identity=backend.identity();assert(identity && identity.value->provider_flags==1 && !identity.value->board_identity_verified);
 Digest id{};backend_bridge_entry(io.bridge,id.data());auto program=backend.program_load(id);assert(program);
 auto duplicate=backend.program_load(id);assert(duplicate);assert(backend.release_program(*duplicate.value));
 auto args=backend.allocate(4,vxnn::AddressDomain::PL,3);assert(args);uint32_t one=1;assert(backend.copy(*args.value,vxnn::Bytes{reinterpret_cast<uint8_t *>(&one),4}));uint64_t live=backend_bridge_live(io.bridge);
 JobDesc job;job.cookie=1;job.program_handle=program.value->handle;job.program_generation=program.value->program_generation;job.device_generation=program.value->device_generation;job.deadline_ns=1000;job.args=*args.value;
 auto submitted=backend.submit(job);assert(submitted && backend_bridge_staging(io.bridge) && backend_bridge_live(io.bridge)==live+1);
 one=2;assert(backend.copy(*args.value,vxnn::Bytes{reinterpret_cast<uint8_t *>(&one),4}));auto timeout=backend.wait(1,0);assert(!timeout && timeout.error->code==-ETIMEDOUT && backend_bridge_live(io.bridge)==live+1);
 backend_bridge_finish(io.bridge,1);assert(backend.wait(1,1000));auto complete=backend.read_completion(*submitted.value);assert(complete && complete.value->cycles_valid && complete.value->raw_cycles==37 && backend_bridge_starts(io.bridge)==1);
 auto detail=backend.detail(*submitted.value);assert(detail && detail.value->submit_ns<=detail.value->start_ns && detail.value->start_ns<=detail.value->done_seen_ns);
 assert(backend.ack_reap(*submitted.value) && backend_bridge_live(io.bridge)==live);
 one=1;assert(backend.copy(*args.value,vxnn::Bytes{reinterpret_cast<uint8_t *>(&one),4}));job.cookie=2;backend_bridge_fault_submit(io.bridge,1);
 auto ambiguous=backend.submit(job);assert(!ambiguous && ambiguous.error->code==-EFAULT && backend_bridge_live(io.bridge)==live+1 && backend_bridge_starts(io.bridge)==2);
 backend_bridge_fault_submit(io.bridge,0);assert(!backend.submit(job) && backend_bridge_starts(io.bridge)==2);
 assert(backend.wait(2,1000));auto recovered=backend.detail(0,2);assert(recovered && recovered.value->sequence==2);
 backend_bridge_fault_ack(io.bridge,1);auto ack_fault=backend.ack_reap(2);assert(!ack_fault && ack_fault.error->code==-EFAULT && backend_bridge_live(io.bridge)==live+1);
 backend_bridge_fault_ack(io.bridge,0);assert(backend.ack_reap(2) && backend_bridge_live(io.bridge)==live);
 assert(backend.release_buffer(*args.value));assert(backend.release_program(*program.value));
 {const uint64_t bytes=(64u<<20)+64;auto large=backend.allocate(bytes,vxnn::AddressDomain::PL,3);assert(large);std::vector<uint8_t> raw(size_t(bytes),0x5a);assert(backend.copy(*large.value,vxnn::Bytes{raw.data(),raw.size()}));auto tail=*large.value;tail.offset=bytes-64;tail.bytes=64;assert(!backend.release_buffer(tail));uint8_t result[64]={};assert(backend.copy(tail,vxnn::MutableBytes{result,64}));for(auto byte:result)assert(byte==0x5a);assert(backend.release_buffer(*large.value));}
 backend_bridge_identity_change(io.bridge);auto changed=backend.identity();assert(!changed && changed.error->code==-ESTALE);assert(!backend.allocate(64,vxnn::AddressDomain::PL,3));
 std::cout<<"UNIFIED_BACKEND_ACTUAL_C_DECODER_ARGS_STAGING_AMBIGUOUS_SUBMIT_ACK_PASS_NOT_BOARD\n";
}

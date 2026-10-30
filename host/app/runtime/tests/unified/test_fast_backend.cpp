#include <vxunified/backend.hpp>
#include "backend_bridge.h"
#include <cassert>
#include <iostream>
using namespace vxunified;
template<class T>auto set_fast(T&b,bool v,int)->decltype(b.set_fast_runtime(v),bool()){return bool(b.set_fast_runtime(v));}
template<class T>bool set_fast(T&,bool,...){return false;}
template<class T>auto is_fast(T&b,int)->decltype(b.fast_runtime(),bool()){return b.fast_runtime();}
template<class T>bool is_fast(T&,...){return false;}
class Transport final:public IoctlTransport{
public:void*bridge;bool available=true,bad_receipt=false;explicit Transport(const char*p):bridge(backend_bridge_create(p)){}~Transport(){backend_bridge_destroy(bridge);}
 int call(unsigned long c,void*p)override{int rc=backend_bridge_call(bridge,c,p);if(!rc){auto*r=static_cast<vxl_compute_v2_request*>(p);if(c==VXL_V2_CAPS&&!available)reinterpret_cast<vxl_v2_caps*>(uintptr_t(r->user_ptr))->flags=0;if(bad_receipt&&(c==VXL_V2_WAIT||(c==VXL_V2_COMPLETE_REAP&&r->flags==VXL_V2_READ)))reinterpret_cast<vxl_v2_completion*>(uintptr_t(r->user_ptr))->version=2;}return rc;}
 uint64_t now_ns()override{return 100;}
};
int main(int argc,char**argv){assert(argc==2);
 {Transport io(argv[1]);io.available=false;ComputeV2Backend b(io);assert(!is_fast(b,0));assert(!set_fast(b,true,0));}
 Transport io(argv[1]);ComputeV2Backend b(io);assert(!is_fast(b,0));assert(set_fast(b,true,0));assert(is_fast(b,0));
 Digest id{};backend_bridge_entry(io.bridge,id.data());auto program=b.program_load(id);assert(program);auto args=b.allocate(4,vxnn::AddressDomain::PL,3);assert(args);uint32_t one=1;assert(b.copy(*args.value,vxnn::Bytes{reinterpret_cast<uint8_t*>(&one),4}));
 JobDesc job;job.cookie=1;job.program_handle=program.value->handle;job.program_generation=program.value->program_generation;job.device_generation=program.value->device_generation;job.deadline_ns=1000;job.args=*args.value;auto submitted=b.submit(job);assert(submitted);assert(!set_fast(b,false,0));backend_bridge_finish(io.bridge,1);assert(b.wait(1,1000));auto d=b.detail(*submitted.value);assert(d&&d.value->version==4&&d.value->reserved0==1);io.bad_receipt=true;assert(!b.detail(*submitted.value));io.bad_receipt=false;assert(b.detail(*submitted.value));assert(backend_bridge_starts(io.bridge)==1);assert(b.ack_reap(*submitted.value));assert(b.release_buffer(*args.value));assert(b.release_program(*program.value));std::cout<<"FAST_BACKEND_NEGOTIATION_RECEIPT_NO_POLICY_SWITCH_STRICT_DEFAULT_HOST_PASS\n";
}

#include <vxunified/service.hpp>
#include "scheduler_fixture.hpp"
#include "backend_bridge.h"
#include <fcntl.h>
#include <unistd.h>
#include <new>
#include <system_error>
// Fault only allocations on the calling test thread after the original error
// object exists. No device job is submitted; return copies are not elided.
static thread_local long allocation_fault=-1;
void*operator new(std::size_t n){if(allocation_fault>=0&&allocation_fault--==0){allocation_fault=-1;throw std::bad_alloc();}void*p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();return p;}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}
struct Transport:IoctlTransport {
 void*bridge;explicit Transport(const char*path):bridge(backend_bridge_create(path)){}~Transport(){backend_bridge_destroy(bridge);}
 int call(unsigned long c,void*p)override{return backend_bridge_call(bridge,c,p);}uint64_t now_ns()override{return 100;}
};
int main(int argc,char**argv){
 require(argc==2,"catalog");char directory[]="/tmp/vx-port-return-XXXXXX";require(mkdtemp(directory)!=nullptr,"own private directory");ServiceConfig config;config.private_dir=directory;auto owner=value(DeviceOwner::acquire(config,[](const std::string&){return Result<int>::success(open("/dev/null",O_RDWR|O_CLOEXEC));}),"one owner");Transport*transport=nullptr;long inject=-1;bool return_error=true;
 const std::string category(160,'C'),detail(320,'D');ExecutionHooks hooks;
 hooks.finish_gpu=[](const WorkItem&,UnifiedBackend&,ExecutionReceipt&){return Result<void>::success();};hooks.retire_request=[](RequestKey,UnifiedBackend&){return Result<void>::success();};
 hooks.non_gpu=[&](const WorkItem&,UnifiedBackend&,vxnn::CpuPool*,ExecutionReceipt&){if(!return_error)return Result<void>::success();auto original=Result<void>::failure({EIO,category,detail});allocation_fault=inject;return original;};
 auto port=std::make_shared<ComputeExecutionPort>(owner,hooks,[&](int fd){require(fd==owner->fd(),"borrow same fd");auto io=std::make_unique<Transport>(argv[1]);transport=io.get();return io;});value(port->probe(owner->fd(),BackendMode::COMPUTE_V2),"original C CAPS only");
 WorkItem work;work.request={"boot","client","callback"};work.app=AppKind::NN;work.kind=WorkKind::TRANSFER;work.node=1;work.operation=CpuTaskRef{"registered.output",1};unsigned bad_allocs=0,wrong_exceptions=0,original_errors=0;
 for(inject=0;inject<12;++inject){
  return_error=true;ExecutionReceipt receipt;
  try{auto result=port->execute(owner->fd(),work,nullptr,receipt);allocation_fault=-1;require(!result&&result.error->code==EIO&&result.error->category==category&&result.error->detail==detail,"original callback error retained");++original_errors;}
  catch(const std::bad_alloc&){allocation_fault=-1;++bad_allocs;}
  catch(const std::exception&e){allocation_fault=-1;++wrong_exceptions;std::fprintf(stderr,"U20_PORT_RETURN_WRONG_EXCEPTION index=%ld what=%s\n",inject,e.what());}
  return_error=false;ExecutionReceipt next;require(bool(port->execute(owner->fd(),work,nullptr,next)),"finished callback reservation cleared after error or exception");
 }
 require(bad_allocs&&original_errors&&!backend_bridge_starts(transport->bridge),"fault injection exercised return copies, no GPU start");
 require(!wrong_exceptions,"U20_PORT_RESULT_COPY_EXCEPTION_ASSERT preserve original bad_alloc instead of lock reentry exception");
 port.reset();owner.reset();unlink((std::string(directory)+"/owner.lock").c_str());rmdir(directory);std::printf("U20_PORT_ERROR_RETURN_COPY_ALLOCATION_ORIGINAL_EXCEPTION_AND_RESERVATION_PASS_HOST_ONLY bad_alloc=%u errors=%u gpu_raw_cycles=null\n",bad_allocs,original_errors);return 0;
}

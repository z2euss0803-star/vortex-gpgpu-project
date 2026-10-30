#include <vxunified/service.hpp>
#include "scheduler_fixture.hpp"
#include "backend_bridge.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
namespace {
struct Transport:IoctlTransport {
 void*bridge;int caps=0,acks=0;bool finish=true;explicit Transport(const char*p):bridge(backend_bridge_create(p)){}~Transport(){backend_bridge_destroy(bridge);}
 int call(unsigned long cmd,void*p)override{
  if(cmd==VXL_V2_CAPS)++caps;
  if(cmd==VXL_V2_WAIT)backend_bridge_finish(bridge,finish?1:0);
  if(cmd==VXL_V2_COMPLETE_REAP&&static_cast<vxl_compute_v2_request*>(p)->flags==VXL_V2_ACK)++acks;
  int rc=backend_bridge_call(bridge,cmd,p);
  // The host C decoder's synthetic counter is not a physical GPU observation.
  if(!rc&&(cmd==VXL_V2_WAIT||(cmd==VXL_V2_COMPLETE_REAP&&static_cast<vxl_compute_v2_request*>(p)->flags==VXL_V2_READ))){auto*r=static_cast<vxl_compute_v2_request*>(p);auto*c=reinterpret_cast<vxl_v2_completion*>(r->user_ptr);c->cycles_valid=0;c->elapsed_raw=0;}
  return rc;
 }
 uint64_t now_ns()override{return 100;}
};
struct Fixture {
 char dir[64]="/tmp/vx-compute-port-XXXXXX";std::shared_ptr<DeviceOwner> owner;std::shared_ptr<ComputeExecutionPort> port;Transport*io=nullptr;int factories=0,finishes=0,retirements=0;int prepares=0;bool readiness=true;bool prepare_error=false,prepare_throw=false,bad_cookie=false,bad_epoch=false;bool check_entry=false,fail_log=false,fail_ack_log=false,bad_output=false,throw_finish=false;std::optional<ExecutionReceipt> saved_error;int raw=0,ack=0;std::shared_ptr<Runtime>rt;std::shared_ptr<UnifiedBackend>backend;ProgramRef program;BufferRef args;
 Fixture(const char*catalog,bool missing_hook=false){require(mkdtemp(dir)!=nullptr,"private native owner fixture");ServiceConfig config;config.private_dir=dir;owner=value(DeviceOwner::acquire(config,[](const std::string&){return Result<int>::success(open("/dev/null",O_RDWR|O_CLOEXEC));}),"owner acquire");ExecutionHooks hooks;hooks.prepare_gpu=[this](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r)->Result<GPUJobDesc>{
 ++prepares;const auto&d=std::get<DeferredGpuRef>(w.operation);if(d.registered_name!="test.controlled.v1")return Result<GPUJobDesc>::failure({EOPNOTSUPP,"ADAPTER","unknown trusted key"});
 Digest entry{};backend_bridge_entry(io->bridge,entry.data());program=value(b.program_load(entry),"dispatch program load");args=value(b.allocate(4,vxnn::AddressDomain::PL,3),"dispatch args create");uint32_t one=1;ok(b.copy(args,vxnn::Bytes{reinterpret_cast<uint8_t*>(&one),4}),"dispatch upload");
 r.program_handle=program.handle;r.program_generation=program.program_generation;r.program_entry_id=program.program_sha;
 if(prepare_throw)throw std::runtime_error("original deferred preparation exception");
 if(prepare_error)return Result<GPUJobDesc>::failure({EIO,"ADAPTER","original prepare error after allocation"});
 GPUJobDesc job;job.program_handle=program.handle;job.program_generation=program.program_generation;job.device_generation=bad_epoch?2:d.device_generation;job.cookie=d.cookie+(bad_cookie?1:0);job.deadline_ns=1000000;job.args=args;return Result<GPUJobDesc>::success(job);
 };hooks.finish_gpu=[this](const WorkItem&w,UnifiedBackend&,ExecutionReceipt&r){++finishes;if(throw_finish)throw std::runtime_error("original post-ACK adapter exception");PublishedResult p;p.execution_complete=true;p.lease.producer=w.request;p.lease.hardware=rthw();p.lease.model=digest(1);p.lease.plan=digest(bad_output?9:2);p.lease.scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;p.lease.lease_tokens={7};ResultArtifact a;a.path="result.bin";a.layout="NCHW";a.shape={1};a.bytes=4;a.sha=digest(7);p.lease.artifacts={a};r.output=p;return Result<void>::success();};hooks.retire_request=[this](RequestKey,UnifiedBackend&b){++retirements;if(!program.handle&&!args.handle)return Result<void>::success();auto a=b.release_buffer(args);if(!a)return a;return b.release_program(program);};if(missing_hook)hooks.prepare_gpu={};port=std::make_shared<ComputeExecutionPort>(owner,hooks,[this,catalog](int fd){require(fd==owner->fd(),"factory borrowed sole fd");++factories;auto p=std::make_unique<Transport>(catalog);io=p.get();return std::unique_ptr<IoctlTransport>(std::move(p));});}
 Digest rthw(){Digest d{};d.fill(1);return d;}
 BackendIdentity start(){auto identity=value(port->probe(owner->fd(),BackendMode::COMPUTE_V2),"read-only native CAPS probe");require(factories==1&&io->caps>=1&&io->acks==0&&backend_bridge_starts(io->bridge)==0,"startup reads CAPS but never starts work");rt=value(Runtime::start(owner,identity,vxnn::EvidenceScope::HOST_PROTOCOL_TEST,port,[this](const ExecutionReceipt&r,DeliveryStage s){if(s==DeliveryStage::EXECUTION_ERROR){saved_error=r;if(check_entry)require(r.program_entry_id==program.program_sha&&nonzero_digest(r.program_entry_id),"WAIT error must retain exact admitted catalog entry without completion");}if(s==DeliveryStage::RAW_RETIREMENT){++raw;require(r.linux_detail&&r.outcome.completion&&io->acks==0,"save raw snapshot before userspace ACK");if(fail_log)return Result<void>::failure({ENOSPC,"EVIDENCE","raw log full"});}if(s==DeliveryStage::ACK_RETIREMENT){++ack;require(io->acks==1&&r.outcome.retired&&r.outcome.acknowledged&&r.retire_end_ns>=r.retire_begin_ns,"ACK retirement and Linux timing retained");if(fail_ack_log)return Result<void>::failure({ENOSPC,"EVIDENCE","ACK log full"});}return Result<void>::success();},[](const PublishedResult&){return Result<void>::success();}),"native port Runtime start");backend=value(port->adapter_backend(),"same backend for admitted adapters");return identity;}
 WorkRequest load(uint64_t epoch=1){auto req=request("deferred",1);req.cpu_bytes=4096;
 struct Deferred:WorkSource{uint64_t epoch;const bool* gate;Deferred(uint64_t e,const bool*g):epoch(e),gate(g){}Result<bool>ready(uint64_t)const override{return Result<bool>::success(*gate);}uint64_t size()const override{return 1;}Digest identity()const override{return digest(51);}Result<WorkItem>get(uint64_t)const override{WorkItem w;w.node=1;w.operation=DeferredGpuRef{"test.controlled.v1",1,epoch,71};return Result<WorkItem>::success(w);}};
 req.source=std::make_shared<Deferred>(epoch,&readiness);return req;}
 ~Fixture(){rt.reset();backend.reset();port.reset();owner.reset();unlink((std::string(dir)+"/owner.lock").c_str());rmdir(dir);}
};
int run(const char*catalog,const std::string&mode){Fixture f(catalog,mode=="missing-hook");f.start();auto req=f.load(mode=="stale"?2:1);
 require(f.prepares==0&&!f.program.handle&&!f.args.handle&&backend_bridge_starts(f.io->bridge)==0,"no adapter program handles or allocations before dispatch");
 auto admitted=f.rt->submit(req);
 if(mode=="stale"){require(!admitted&&admitted.error->code==ESTALE&&f.prepares==0,"stale deferred epoch rejected without preparation");ok(f.rt->shutdown(),"empty shutdown");}
 else {
  require(bool(admitted),"valid deferred logical work must be admitted without resident handles");require(f.prepares==0,"admission never prepares GPU");
  if(mode=="blocked"){
   f.readiness=false;auto idle=f.rt->run_one();require(!idle&&idle.error->code==EAGAIN&&f.prepares==0&&backend_bridge_starts(f.io->bridge)==0,"Runtime BoundSource must preserve metadata backpressure without activating preparation");f.readiness=true;ok(f.rt->run_one(),"source becomes eligible");require(f.prepares==1&&f.io->acks==1,"exactly one preparation after release");ok(f.rt->reap_result(req.request),"retire unblocked result");ok(f.rt->shutdown(),"unblocked shutdown");puts("U12_DEFERRED_blocked_PASS_HOST_C_PROTOCOL_NOT_GPU gpu_raw_cycles=null");return 0;
  }
  if(mode=="cancel"){
   ok(f.rt->cancel(req.request),"cancel deferred work before dispatch");auto idle=f.rt->run_one();require(!idle&&idle.error->code==EAGAIN,"already cancelled queue has no GPU dispatch");require(f.prepares==0&&backend_bridge_starts(f.io->bridge)==0&&value(f.rt->status(req.request),"cancelled state")==RequestStatus::CANCELLED,"queued cancellation never loads program");ok(f.rt->reap_result(req.request),"reap cancelled unmaterialized backing");require(f.retirements==1,"unmaterialized cancellation retires without resource guesses");ok(f.rt->shutdown(),"cancelled empty shutdown");
   puts("U12_DEFERRED_cancel_PASS_HOST_C_PROTOCOL_NOT_GPU gpu_raw_cycles=null");return 0;
  }
  if(mode=="missing-hook"){
   auto fail=f.rt->run_one();require(!fail&&fail.error->code==EOPNOTSUPP&&f.prepares==0&&backend_bridge_starts(f.io->bridge)==0,"no default or guessed deferred preparation");require(f.saved_error&&!f.rt->retry_delivery()&&!f.rt->shutdown(),"missing hook retains original failed request");
   puts("U12_DEFERRED_missing_hook_PASS_HOST_C_PROTOCOL_NOT_GPU gpu_raw_cycles=null");return 0;
  }
  f.prepare_throw=mode=="prepare-throw";f.prepare_error=mode=="prepare-error";f.bad_cookie=mode=="cookie";f.bad_epoch=mode=="epoch";
  if(mode=="flow"){
   f.fail_log=true;auto first=f.rt->run_one();require(!first&&first.error->code==ENOSPC,"raw persistence fault retains active resolved job");require(f.prepares==1&&f.io->acks==0&&backend_bridge_starts(f.io->bridge)==1,"one dispatch prep and C submit, no premature ACK");f.fail_log=false;ok(f.rt->retry_delivery(),"retry only delivery");require(f.prepares==1&&f.io->acks==1&&f.finishes==1&&backend_bridge_starts(f.io->bridge)==1,"no duplicate prep/submit/ACK");ok(f.rt->reap_result(req.request),"retire bound resources");require(f.retirements==1,"one release after durable completion");ok(f.rt->shutdown(),"deferred shutdown");
  }else{
   auto first=f.rt->run_one();require(!first&&first.error->code==((f.prepare_error||f.prepare_throw)?EIO:ESTALE)&&f.saved_error,"prepare or resolved identity error retained");require(f.saved_error->prepare_begin_ns&&f.saved_error->prepare_end_ns>=f.saved_error->prepare_begin_ns,"prepare interval retained on failure");require(f.prepares==1&&f.io->acks==0&&backend_bridge_starts(f.io->bridge)==0&&f.finishes==0&&f.retirements==0,"no submit/release after preparation failure");require(f.saved_error->submitted_cookie==71&&f.saved_error->device_generation==1,"logical identity retained on pre-submit error");require(!f.rt->retry_delivery()&&!f.rt->shutdown(),"failed preparation resources remain owned without retry");
  }
 }
 std::printf("U12_DEFERRED_%s_PASS_HOST_C_PROTOCOL_NOT_GPU gpu_raw_cycles=null\n",mode.c_str());return 0;
}
}
int main(int n,char**v){if(n!=3)return 2;return run(v[1],v[2]);}

#include <vxunified/service.hpp>
#include "scheduler_fixture.hpp"
#include "backend_bridge.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <type_traits>
namespace {
struct Transport:IoctlTransport {
 void*bridge;int caps=0,acks=0,waits=0;bool finish=true,interrupt_next_wait=false;explicit Transport(const char*p):bridge(backend_bridge_create(p)){}~Transport(){backend_bridge_destroy(bridge);}
 int call(unsigned long cmd,void*p)override{
  if(cmd==VXL_V2_CAPS)++caps;
  if(cmd==VXL_V2_WAIT){++waits;backend_bridge_finish(bridge,finish?1:0);backend_bridge_interrupt_wait(bridge,interrupt_next_wait?1:0);interrupt_next_wait=false;}
  if(cmd==VXL_V2_COMPLETE_REAP&&static_cast<vxl_compute_v2_request*>(p)->flags==VXL_V2_ACK)++acks;
  int rc=backend_bridge_call(bridge,cmd,p);
  if(cmd==VXL_V2_WAIT)backend_bridge_interrupt_wait(bridge,0);
  // The host C decoder's synthetic counter is not a physical GPU observation.
  if(!rc&&(cmd==VXL_V2_WAIT||(cmd==VXL_V2_COMPLETE_REAP&&static_cast<vxl_compute_v2_request*>(p)->flags==VXL_V2_READ))){auto*r=static_cast<vxl_compute_v2_request*>(p);auto*c=reinterpret_cast<vxl_v2_completion*>(r->user_ptr);c->cycles_valid=0;c->elapsed_raw=0;}
  return rc;
 }
 uint64_t now_ns()override{return 100;}
};
struct Fixture {
 char dir[64]="/tmp/vx-compute-port-XXXXXX";std::shared_ptr<DeviceOwner> owner;std::shared_ptr<ComputeExecutionPort> port;Transport*io=nullptr;int factories=0,finishes=0,retirements=0;bool check_entry=false,fail_log=false,fail_ack_log=false,fail_error_log=false,bad_output=false,throw_finish=false,fail_request_retire=false;std::optional<ExecutionReceipt> saved_error;int raw=0,ack=0;std::shared_ptr<Runtime>rt;std::shared_ptr<UnifiedBackend>backend;ProgramRef program;BufferRef args;
 Fixture(const char*catalog){require(mkdtemp(dir)!=nullptr,"private native owner fixture");ServiceConfig config;config.private_dir=dir;owner=value(DeviceOwner::acquire(config,[](const std::string&){return Result<int>::success(open("/dev/null",O_RDWR|O_CLOEXEC));}),"owner acquire");ExecutionHooks hooks;hooks.finish_gpu=[this](const WorkItem&w,UnifiedBackend&,ExecutionReceipt&r){++finishes;if(throw_finish)throw std::runtime_error("original post-ACK adapter exception");PublishedResult p;p.execution_complete=true;p.lease.producer=w.request;p.lease.hardware=rthw();p.lease.model=digest(1);p.lease.plan=digest(bad_output?9:2);p.lease.scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;p.lease.lease_tokens={7};ResultArtifact a;a.path="result.bin";a.layout="NCHW";a.shape={1};a.bytes=4;a.sha=digest(7);p.lease.artifacts={a};r.output=p;return Result<void>::success();};hooks.retire_request=[this](RequestKey,UnifiedBackend&b){++retirements;auto a=b.release_buffer(args);if(!a)return a;if(fail_request_retire)return Result<void>::failure({EIO,"RETIRE_PARTIAL","original partial request retirement failure"});return b.release_program(program);};port=std::make_shared<ComputeExecutionPort>(owner,hooks,[this,catalog](int fd){require(fd==owner->fd(),"factory borrowed sole fd");++factories;auto p=std::make_unique<Transport>(catalog);io=p.get();return std::unique_ptr<IoctlTransport>(std::move(p));});}
 Digest rthw(){Digest d{};d.fill(1);return d;}
 BackendIdentity start(){auto identity=value(port->probe(owner->fd(),BackendMode::COMPUTE_V2),"read-only native CAPS probe");require(factories==1&&io->caps>=1&&io->acks==0&&backend_bridge_starts(io->bridge)==0,"startup reads CAPS but never starts work");rt=value(Runtime::start(owner,identity,vxnn::EvidenceScope::HOST_PROTOCOL_TEST,port,[this](const ExecutionReceipt&r,DeliveryStage s){if(s==DeliveryStage::EXECUTION_ERROR){saved_error=r;if(fail_error_log)return Result<void>::failure({ENOSPC,"EVIDENCE","error log full"});if(check_entry)require(r.program_entry_id==program.program_sha&&nonzero_digest(r.program_entry_id),"WAIT error must retain exact admitted catalog entry without completion");}if(s==DeliveryStage::RAW_RETIREMENT){++raw;require(r.linux_detail&&r.outcome.completion&&io->acks==0,"save raw snapshot before userspace ACK");if(fail_log)return Result<void>::failure({ENOSPC,"EVIDENCE","raw log full"});}if(s==DeliveryStage::ACK_RETIREMENT){++ack;require(io->acks==1&&r.outcome.retired&&r.outcome.acknowledged&&r.retire_end_ns>=r.retire_begin_ns,"ACK retirement and Linux timing retained");if(fail_ack_log)return Result<void>::failure({ENOSPC,"EVIDENCE","ACK log full"});}return Result<void>::success();},[](const PublishedResult&){return Result<void>::success();}),"native port Runtime start");backend=value(port->adapter_backend(),"same backend for admitted adapters");return identity;}
 WorkRequest load(){Digest entry{};backend_bridge_entry(io->bridge,entry.data());program=value(backend->program_load(entry),"frozen program");args=value(backend->allocate(4,vxnn::AddressDomain::PL,3),"args object");uint32_t one=1;ok(backend->copy(args,vxnn::Bytes{reinterpret_cast<uint8_t*>(&one),4}),"args upload");auto req=request("gpu",1);req.cpu_bytes=4096;struct GpuSource:WorkSource{GPUJobDesc j;uint64_t size()const override{return 1;}Digest identity()const override{return digest(5);}Result<WorkItem>get(uint64_t)const override{WorkItem w;w.node=1;w.kind=WorkKind::GPU;w.operation=j;return Result<WorkItem>::success(w);}};auto source=std::make_shared<GpuSource>();source->j.program_handle=program.handle;source->j.program_generation=program.program_generation;source->j.device_generation=program.device_generation;source->j.cookie=1;source->j.deadline_ns=1000000;source->j.args=args;req.source=source;return req;}
 ~Fixture(){rt.reset();backend.reset();port.reset();owner.reset();unlink((std::string(dir)+"/owner.lock").c_str());rmdir(dir);}
};
template<class T,class=void>struct RecoveryChecks {
 static int run(Fixture&,const WorkRequest&,const std::string&){require(false,"same-owner recovery and diagnostic interface required");return 1;}
};
template<class T>struct RecoveryChecks<T,std::void_t<decltype(std::declval<T&>().recover_pending(uint64_t{})),decltype(std::declval<T&>().diagnostic(RequestKey{}))>> {
 static int run(Fixture&f,const WorkRequest&req,const std::string&mode){
  // A recoverable OS interruption occurs before the hardware execution budget
  // expires. A real elapsed budget must remain a hardware timeout/quarantine.
  const bool interrupted=mode!="late-unknown"&&mode!="late-identity";
  T&rt=*f.rt;f.io->finish=false;f.io->interrupt_next_wait=interrupted;auto first=rt.run_one();require(!first&&first.error->code==(interrupted?-EINTR:-ETIMEDOUT),"original WAIT interruption or actual timeout preserved");
  auto original=*first.error;auto diagnostic=value(rt.diagnostic(req.request),"operation fault diagnostic");
  require(diagnostic.status==RequestStatus::FAILED&&diagnostic.execution_error&&diagnostic.execution_error->code==original.code&&diagnostic.device_pending&&diagnostic.retirement_unknown,"operation failure visible separately from device UNKNOWN");
  auto early_reap=rt.reap_result(req.request);
  require(!early_reap&&early_reap.error->code==EBUSY,"failed operation cannot reap unretired backing");
  require(f.retirements==0,"failed operation cannot invoke resource retirement");
  require(rt.stats().scheduler.pending==1&&rt.stats().scheduler.active_gpu==1,"one GPU token remains retained until proven ACK retirement");
  if(mode=="late-unknown"||mode=="late-identity"){
   if(mode=="late-identity")backend_bridge_identity_change(f.io->bridge);
   auto recovery=rt.recover_pending(1000000);require(!recovery&&f.io->acks==0&&f.finishes==0&&backend_bridge_starts(f.io->bridge)==1,"bounded still-unknown WAIT never ACKs or resubmits");
   require(value(rt.diagnostic(req.request),"unknown diagnostic").execution_error->code==original.code&&!rt.shutdown(),"original error and pending resources preserved");backend_bridge_finish(f.io->bridge,1);return 0;
  }
  if(mode=="late-error-log"){
   f.fail_error_log=true;f.io->interrupt_next_wait=true;auto observed=rt.recover_pending(1000000);require(!observed&&rt.stats().phase==RuntimePhase::DELIVERY_BLOCKED,"recovery error evidence failure blocks further observation");int waits=f.io->waits;
   require(!rt.recover_pending(1000000)&&f.io->waits==waits&&f.io->acks==0,"unsaved recovery failure cannot issue another WAIT");
   f.fail_error_log=false;require(!rt.retry_delivery()&&f.io->waits==waits&&f.io->acks==0,"evidence-only error retry retains original failure without I/O");
  }
  f.io->finish=true;f.fail_log=mode=="late-log";f.fail_request_retire=mode=="late-retire-fault";
  if(mode=="late-ack-fault")backend_bridge_fault_ack(f.io->bridge,1);
  auto recovery=rt.recover_pending(1000000);
  if(mode=="late-log"){
   require(!recovery&&recovery.error->code==ENOSPC&&f.io->acks==0&&f.finishes==0,"late raw snapshot must be durable before ACK");
   f.fail_log=false;ok(rt.retry_delivery(),"retry late evidence without another WAIT or submit");
  }else if(mode=="late-ack-fault"){
   require(!recovery&&recovery.error->code==-EFAULT&&f.io->acks==1&&f.finishes==0,"ambiguous late ACK retains resources");
   require(!rt.recover_pending(1000000)&&f.io->acks==1&&f.retirements==0,"ambiguous ACK never repeated");return 0;
  }else if(mode=="late-retire-fault"){
   require(!recovery&&recovery.error->code==EIO&&f.io->acks==1&&f.retirements==1,"partial adapter release failure retained after one ACK");
   auto d=value(rt.diagnostic(req.request),"partial retirement diagnostic");require(!d.device_pending&&d.retirement_unknown&&d.resources_retained&&!d.recovered,"known device ACK does not prove full adapter cleanup");
   require(!rt.recover_pending(1000000)&&f.io->acks==1&&f.retirements==1,"uncertain resource callback cannot be repeated");return 0;
  }else ok(recovery,"late same-owner recovery");
  diagnostic=value(rt.diagnostic(req.request),"recovered failed operation");
  require(diagnostic.status==RequestStatus::FAILED&&diagnostic.recovered&&!diagnostic.device_pending&&!diagnostic.retirement_unknown&&diagnostic.execution_error&&diagnostic.execution_error->code==original.code,"late cleanup cannot turn original operation into PASS");
  require(f.io->acks==1&&f.finishes==1&&f.retirements==1&&backend_bridge_starts(f.io->bridge)==1&&!rt.stats().scheduler.pending,"one original START/ACK and proven backing retirement");
  require(!rt.take_result(req.request)&&value(rt.first_error(),"first error after recovery").code==original.code,"no failed operation output or overwritten first error");
  require(!rt.recover_pending(1000000)&&f.io->acks==1,"completed cleanup cannot repeat ACK");ok(rt.reap_result(req.request),"forget recovered failure metadata");ok(rt.shutdown(),"recovered request shuts down");return 0;
 }
};
int run(const char*catalog,const std::string&mode){Fixture f(catalog);f.start();auto req=f.load();
 if(mode=="late-tail"){
  struct Tail:WorkSource{std::shared_ptr<const WorkSource>first;explicit Tail(std::shared_ptr<const WorkSource>s):first(std::move(s)){}uint64_t size()const override{return 2;}Digest identity()const override{return digest(5);}Result<WorkItem>get(uint64_t n)const override{auto w=first->get(0);if(!w)return w;auto item=*w.value;item.node+=n;std::get<GPUJobDesc>(item.operation).cookie+=n;return Result<WorkItem>::success(std::move(item));}};
  req.source=std::make_shared<Tail>(req.source);
 }
 value(f.rt->submit(req),"native request submit");if(mode.find("late-")==0){int code=RecoveryChecks<Runtime>::run(f,req,mode);std::printf("S8_SAME_OWNER_%s_HOST_C_DECODER_NOT_BOARD_PASS_GPU_CYCLES_NULL\n",mode.c_str());return code;}if(mode=="flow"||mode=="ack-log"){
 f.fail_log=mode=="flow";f.fail_ack_log=mode=="ack-log";auto first=f.rt->run_one();require(!first&&first.error->code==ENOSPC,"evidence failure before publication");require(backend_bridge_starts(f.io->bridge)==1,"exactly one actual C submit/start");if(mode=="flow")require(f.io->acks==0&&f.finishes==0,"no ACK or result before raw receipt durable");else require(f.io->acks==1&&f.finishes==1,"ACK completed once before ACK log blocked");f.fail_log=false;f.fail_ack_log=false;ok(f.rt->retry_delivery(),"retry delivery without reexecution");require(f.io->acks==1&&f.finishes==1&&backend_bridge_starts(f.io->bridge)==1,"never repeat GPU or ACK on delivery retry");require(value(f.rt->status(req.request),"terminal")==RequestStatus::COMPLETED,"complete after durable ACK");ok(f.rt->reap_result(req.request),"retire exact request resources");require(f.retirements==1,"adapter resources released once");ok(f.rt->shutdown(),"native shutdown");
 }else if(mode=="timeout"||mode=="timeout-evidence"||mode=="entry-timeout"){f.check_entry=mode=="entry-timeout";f.io->finish=false;auto fail=f.rt->run_one();require(!fail&&fail.error->code==-ETIMEDOUT,"wait timeout returned unchanged");require(f.io->acks==0&&f.finishes==0&&!f.rt->retry_delivery()&&!f.rt->shutdown(),"timeout is not retirement or auto retry");if(mode=="timeout-evidence")require(f.saved_error&&f.saved_error->submitted_cookie==1&&f.saved_error->submitted_sequence==1&&f.saved_error->program_handle==f.program.handle&&f.saved_error->device_generation==f.program.device_generation&&f.saved_error->submit_begin_ns&&f.saved_error->submit_end_ns>=f.saved_error->submit_begin_ns&&f.saved_error->wait_begin_ns&&f.saved_error->wait_end_ns>=f.saved_error->wait_begin_ns,"WAIT timeout must preserve admitted cookie/sequence and submit/wait intervals");backend_bridge_finish(f.io->bridge,1); // Only physical fixture teardown; no service retry.
 }else if(mode=="ack-fault"){backend_bridge_fault_ack(f.io->bridge,1);auto fail=f.rt->run_one();require(!fail&&fail.error->code==-EFAULT,"ambiguous ACK failure returned");require(f.io->acks==1&&f.finishes==0&&backend_bridge_starts(f.io->bridge)==1&&!f.rt->retry_delivery(),"no automatic repeat of ambiguous ACK");}
 else if(mode=="bad-output"){f.bad_output=true;auto failed=f.rt->run_one();require(!failed&&failed.error->code==EPROTO&&!f.rt->take_result(req.request),"post-ACK adapter output identity must be revalidated before publication");require(f.rt->stats().phase==RuntimePhase::FAULTED&&f.io->acks==1&&f.finishes==1&&f.saved_error,"invalid result quarantines with evidence after one ACK");}
 else if(mode=="finish-throw"){f.throw_finish=true;auto failed=f.rt->run_one();require(!failed&&failed.error->code==EIO&&failed.error->detail=="original post-ACK adapter exception"&&f.saved_error,"post-ACK adapter exception must retain original execution evidence");require(f.rt->stats().phase==RuntimePhase::FAULTED&&f.io->acks==1&&f.saved_error->outcome.acknowledged&&!f.rt->retry_delivery(),"exception cannot retry an already ACKed job");}
 else {return 2;}
 std::printf("U10_NATIVE_COMPUTE_%s_HOST_C_DECODER_NOT_BOARD_PASS_GPU_CYCLES_NULL\n",mode.c_str());return 0;}
}
int main(int n,char**v){if(n!=3)return 2;return run(v[1],v[2]);}

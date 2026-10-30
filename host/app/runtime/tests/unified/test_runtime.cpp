#include <vxunified/service.hpp>
#include "scheduler_fixture.hpp"
#include <atomic>
#include <condition_variable>
#include <fcntl.h>
#include <mutex>
#include <map>
#include <thread>
#include <type_traits>
#include <unistd.h>
namespace {
BackendIdentity identity(){BackendIdentity i;i.hardware_contract=digest(11);i.bit=digest(12);i.xsa=digest(13);i.compatible=digest(14);i.device_generation=1;i.provider_flags=1;return i;}
WorkRequest job(const char*id="one"){auto r=request(id,1,WorkKind::CPU);r.cpu_bytes=4096;r.resident_bytes=1024;return r;}
struct Port:ExecutionPort {
 BackendIdentity bound=identity();std::map<AppKind,Digest> output_models;
 int expected_fd=-1;std::atomic<int> executes{0},retires{0};bool pause=false,entered=false,resume=false,fail_retire=false,unknown=false,throw_execute=false,bad_output=false,offline_cycles=false;
 std::mutex mutex;std::condition_variable cv;
 std::function<void()>on_retire;
 std::function<Result<void>(const WorkItem&,ExecutionReceipt&)>output_hook;
 Result<BackendIdentity> probe(int fd,BackendMode mode)override{if(fd!=expected_fd||mode!=BackendMode::COMPUTE_V2)return Result<BackendIdentity>::failure({EXDEV,"PORT","wrong borrowed owner"});return Result<BackendIdentity>::success(bound);}
 Result<void> execute(int fd,const WorkItem&w,vxnn::CpuPool*pool,ExecutionReceipt&receipt)override{
  ++executes;if(fd!=expected_fd||!pool)return Result<void>::failure({EBADF,"PORT","lost unique fd or shared CPU pool"});
  if(unknown){receipt.outcome=outcome(w);receipt.outcome.retired=false;receipt.outcome.acknowledged=false;receipt.outcome.status=RequestStatus::RUNNING;vxl_v2_completion detail{};detail.cookie=901;detail.sequence=81;detail.submit_ns=1234;receipt.linux_detail=detail;if(throw_execute)throw std::runtime_error("original executor exception 901");return Result<void>::failure({ETIMEDOUT,"PORT","original unknown execution; do not retry"});}
  {std::unique_lock<std::mutex>lock(mutex);entered=true;cv.notify_all();if(pause)cv.wait(lock,[&]{return resume;});}
  float x[11]={},y[11];auto g=vxnn::gelu_erf_pool_reserved(*pool,x,y,11,w.cancel_control,w.pending_token);
  receipt.outcome=outcome(w);
  if(offline_cycles){Completion c;c.cookie=1;c.sequence=1;c.cycles_valid=true;c.raw_cycles=1;receipt.outcome.completion=c;}
  if(!g){if(g.error->code!=ECANCELED)return Result<void>::failure(*g.error);receipt.outcome.status=RequestStatus::CANCELLED;receipt.outcome.result=ECANCELED;return Result<void>::success();}
  for(auto v:y)if(v!=0)return Result<void>::failure({ERANGE,"PORT","independent zero GELU reference"});
  PublishedResult p;p.execution_complete=true;p.lease.producer=w.request;p.lease.app=w.app;p.lease.hardware=bound.hardware_contract;p.lease.model=output_models.count(w.app)?output_models.at(w.app):digest(1);p.lease.plan=digest(bad_output?9:2);p.lease.scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;p.lease.lease_tokens={99};ResultArtifact a;a.path="final.bin";a.layout="NCHW";a.sha=digest(7);a.shape={11};a.bytes=44;p.lease.artifacts={a};p.raw_legacy_json="{\"counter\":9007199254740993}";receipt.output=p;if(output_hook)return output_hook(w,receipt);return Result<void>::success();
 }
 Result<void> retire_request(int fd,RequestKey)override{if(fd!=expected_fd)return Result<void>::failure({EBADF,"PORT","wrong retire fd"});if(fail_retire)return Result<void>::failure({EBUSY,"PORT","original unknown retirement"});++retires;if(on_retire)on_retire();return Result<void>::success();}
};
struct Fixture {
 char directory[64]="/tmp/vx-runtime-XXXXXX";std::atomic<int>opens{0},closes{0};std::shared_ptr<DeviceOwner>owner;std::shared_ptr<Port>port=std::make_shared<Port>();
 bool fail_work=false,fail_output=false,pause_state=false,state_entered=false,state_resume=false;int raw_logs=0,state_logs=0,result_logs=0,error_logs=0;bool check_binding=false;std::optional<ExecutionReceipt> saved_error;
 Fixture(){require(mkdtemp(directory)!=nullptr,"runtime private directory");ServiceConfig c;c.private_dir=directory;auto own=DeviceOwner::acquire(c,[&](const std::string&){++opens;return Result<int>::success(open("/dev/null",O_RDWR|O_CLOEXEC));},[&](int fd){++closes;close(fd);});owner=value(own,"runtime test owner");port->expected_fd=owner->fd();}
 std::shared_ptr<Runtime> start(){return value(Runtime::start(owner,port->bound,vxnn::EvidenceScope::HOST_PROTOCOL_TEST,port,[&](const ExecutionReceipt&r,DeliveryStage stage){if(check_binding){auto&i=r.binding;auto b=identity();require(i.app==AppKind::NN&&i.hardware==b.hardware_contract&&i.bit==b.bit&&i.xsa==b.xsa&&i.compatible==b.compatible&&i.device_generation==b.device_generation&&i.provider_flags==b.provider_flags&&i.model==digest(1)&&i.plan==digest(2)&&i.input==digest(3)&&i.source==job().source->identity()&&i.input_bytes==4,"every persisted event must carry full admitted execution binding");}if(stage==DeliveryStage::RAW_RETIREMENT)++raw_logs;else if(stage==DeliveryStage::EXECUTION_ERROR){++error_logs;saved_error=r;}else ++state_logs;if(stage==DeliveryStage::REQUEST_STATE&&pause_state){std::unique_lock<std::mutex>lock(port->mutex);state_entered=true;port->cv.notify_all();port->cv.wait(lock,[&]{return state_resume;});}if(fail_work)return Result<void>::failure({ENOSPC,"EVIDENCE","original work log failure"});if(r.output&&r.output->raw_legacy_json!="{\"counter\":9007199254740993}")return Result<void>::failure({EPROTO,"TEST","legacy bytes changed"});return Result<void>::success();},[&](const PublishedResult&p){if(check_binding)require(p.binding&&p.binding->source==job().source->identity()&&p.binding->bit==identity().bit&&p.binding->input==digest(3),"published result must retain admitted source/input/platform binding");++result_logs;return fail_output?Result<void>::failure({EIO,"EVIDENCE","original output log failure"}):Result<void>::success();}),"runtime READY without execution");}
 ~Fixture(){owner.reset();unlink((std::string(directory)+"/owner.lock").c_str());rmdir(directory);}
};
int flow(){
 Fixture f;auto rt=f.start();require(f.opens==1&&f.port->executes==0&&rt->stats().phase==RuntimePhase::READY,"startup must not run work");auto r=job();value(rt->submit(r),"submit admitted");require(value(rt->status(r.request),"queued")==RequestStatus::QUEUED,"submit is not execution");value(rt->submit(r),"dedup queued");require(rt->stats().scheduler.pending==4,"one borrowed CPU reservation");
 f.fail_work=true;auto failed=rt->run_one();require(!failed&&failed.error->code==ENOSPC,"raw work evidence failure returned");require(f.port->executes==1&&rt->stats().phase==RuntimePhase::DELIVERY_BLOCKED,"do not rerun work");require(!rt->take_result(r.request)&&rt->stats().scheduler.pending==4,"no success before evidence and retirement delivery");require(!rt->run_one()&&f.port->executes==1,"blocked dispatch");
 f.fail_work=false;f.fail_output=true;auto output_failure=rt->retry_delivery();require(!output_failure&&output_failure.error->code==EIO&&f.port->executes==1,"result evidence failure retained without re-execution");require(value(rt->status(r.request),"not yet complete")==RequestStatus::RUNNING,"scheduler completion alone cannot publish success");
 f.fail_output=false;ok(rt->retry_delivery(),"exact pending delivery recovers");auto result=value(rt->take_result(r.request),"durable result");require(result.raw_legacy_json=="{\"counter\":9007199254740993}"&&!result.gpu_raw_cycles,"legacy bytes preserved and no offline GPU cycles");require(value(rt->status(r.request),"complete")==RequestStatus::COMPLETED,"visible completion after persistence");value(rt->submit(r),"dedup terminal");require(value(rt->status(r.request),"old terminal")==RequestStatus::COMPLETED&&f.port->executes==1,"dedup returns old state");auto changed=r;changed.cpu_bytes++;auto collision=rt->submit(changed);require(!collision&&collision.error->code==EEXIST,"changed footprint cannot reuse key");
 auto dp=value(rt->retain_result(r.request,Consumer::DP),"DP hold");ok(rt->reap_result(r.request),"client reap with DP hold");require(f.port->retires==0&&rt->stats().resident_models==1,"DP pins model/input owner budget");require(!rt->release_result(dp,false),"no early DP release");f.port->fail_retire=true;require(!rt->release_result(dp,true)&&rt->stats().resident_models==1,"retire failure pins budgets");f.port->fail_retire=false;ok(rt->release_result(dp,true),"DP retired");require(f.port->retires==1&&rt->stats().input_bytes==0&&rt->stats().resident_models==0,"release budgets once");ok(rt->reap_result(r.request),"explicit forget after display retirement");require(!rt->status(r.request),"explicit index forget");ok(rt->shutdown(),"shutdown drained");puts("U10_RUNTIME_DURABLE_DELIVERY_ONE_EXECUTION_DP_BUDGET_PASS_NOT_BOARD");return 0;
}
int limits(){
 Fixture f;auto rt=f.start();auto a=job("a");a.input_bytes=40ULL<<20;a.cpu_bytes=40ULL<<20;value(rt->submit(a),"first input");auto b=job("b");b.input_bytes=25ULL<<20;b.cpu_bytes=25ULL<<20;auto over=rt->submit(b);require(!over&&over.error->code==ENOSPC,"input64MiB shared");b.input_bytes=24ULL<<20;b.cpu_bytes=24ULL<<20;b.model=digest(8);value(rt->submit(b),"second resident model");auto c=job("c");c.input_bytes=0;c.model=digest(9);auto third=rt->submit(c);require(!third&&third.error->code==ENOSPC,"resident models2 shared");auto changed=a;changed.resident_bytes++;auto same=rt->submit(changed);require(!same&&same.error->code==EEXIST,"resident footprint identity");
 ok(rt->cancel(a.request),"cancel queued a");require(f.port->executes==0,"queued cancellation never computes");ok(rt->reap_result(a.request),"release cancelled a");ok(rt->cancel(b.request),"cancel queued b");ok(rt->reap_result(b.request),"release cancelled b");auto huge=job("huge");huge.cpu_bytes=1ULL<<30;auto memory=rt->submit(huge);require(!memory&&memory.error->code==ENOSPC,"service1GiB includes overhead");auto unsupported=job("p8");unsupported.app=AppKind::P8;auto hw=rt->submit(unsupported);require(!hw&&hw.error->code==EOPNOTSUPP,"S8 cannot admit P8");require(rt->stats().scheduler.indexed==0,"rejections do not enter scheduler");ok(rt->shutdown(),"empty shutdown");puts("U10_RUNTIME_GLOBAL_INPUT_MEMORY_MODELS_AND_HARDWARE_PASS_NOT_BOARD");return 0;
}
int cancel_active(){
 Fixture f;auto rt=f.start();f.port->pause=true;auto r=job();value(rt->submit(r),"active request");std::atomic<int>pump{-1};std::thread execution([&]{auto out=rt->run_one();pump=out?0:out.error->code;});{std::unique_lock<std::mutex>lock(f.port->mutex);f.port->cv.wait(lock,[&]{return f.port->entered;});}
 ok(rt->cancel(r.request),"cancel active");require(value(rt->status(r.request),"pending cancel")==RequestStatus::CANCEL_PENDING,"cancel cannot infer retirement");require(rt->stats().scheduler.pending==4&&!rt->reap_result(r.request),"active retains budget and backing");{std::lock_guard<std::mutex>lock(f.port->mutex);f.port->resume=true;f.port->cv.notify_all();}execution.join();require(pump==0&&value(rt->status(r.request),"cancelled")==RequestStatus::CANCELLED,"late CPU completion cannot resurrect");require(!rt->take_result(r.request)&&rt->stats().scheduler.pending==0,"no cancelled output publication");ok(rt->reap_result(r.request),"cancelled retirement");ok(rt->shutdown(),"shutdown");puts("U10_RUNTIME_ACTIVE_CANCEL_DRAINS_SHARED_POOL_PASS_NOT_BOARD");return 0;
}
int bad_output(){Fixture f;auto rt=f.start();f.port->bad_output=true;auto r=job();value(rt->submit(r),"bad-output request");auto out=rt->run_one();require(!out&&out.error->code==EPROTO,"bad output rejected");require(rt->stats().phase==RuntimePhase::FAULTED,"bad output must close admission and execution");auto other=job("other");require(!rt->submit(other),"no successor after untrusted output");require(!rt->run_one()&&f.port->executes==1,"no automatic rerun");puts("U10_RUNTIME_BAD_OUTPUT_FAULT_PINS_OWNER_PASS_NOT_BOARD");return 0;}
int late_cancel(){Fixture f;auto rt=f.start();f.pause_state=true;auto r=job();value(rt->submit(r),"late-cancel request");std::atomic<int>code{-1};std::thread worker([&]{auto out=rt->run_one();code=out?0:out.error->code;});{std::unique_lock<std::mutex>lock(f.port->mutex);f.port->cv.wait(lock,[&]{return f.state_entered;});}ok(rt->cancel(r.request),"late cancellation");require(value(rt->status(r.request),"durable completion pending")==RequestStatus::RUNNING,"late cancellation cannot change an already completed raw operation or publish early");require(!rt->reap_result(r.request),"cannot reap undelivered completion");{std::lock_guard<std::mutex>lock(f.port->mutex);f.state_resume=true;f.port->cv.notify_all();}worker.join();require(code==0&&value(rt->status(r.request),"delivered")==RequestStatus::COMPLETED,"original success delivered once");ok(rt->reap_result(r.request),"reap");ok(rt->shutdown(),"shutdown");puts("U10_RUNTIME_LATE_CANCEL_DURABILITY_BOUNDARY_PASS_NOT_BOARD");return 0;}
int expiry(){Fixture f;auto rt=f.start();auto r=job();timespec t{};require(clock_gettime(CLOCK_MONOTONIC,&t)==0,"clock");r.queue_deadline_ns=uint64_t(t.tv_sec)*1000000000ULL+t.tv_nsec+1000000ULL;value(rt->submit(r),"unexpired admission");usleep(20000);ok(rt->run_one(),"publish expired queued request without execution");require(value(rt->status(r.request),"expired")==RequestStatus::FAILED&&f.port->executes==0&&f.raw_logs==0&&f.state_logs==1,"queued timeout is a control event, not GPU completion");ok(rt->reap_result(r.request),"expired resources");ok(rt->shutdown(),"shutdown");puts("U10_RUNTIME_QUEUE_EXPIRY_VISIBLE_WITHOUT_EXECUTION_PASS_NOT_BOARD");return 0;}
int original_error(){Fixture f;auto rt=f.start();auto r=job();value(rt->submit(r),"log-failure request");f.fail_work=true;require(!rt->run_one(),"expected log failure");auto e=value(rt->first_error(),"first error query");require(e.code==ENOSPC&&e.category=="EVIDENCE"&&e.detail=="original work log failure","original error bytes retained");f.fail_work=false;ok(rt->retry_delivery(),"resume evidence");require(value(rt->first_error(),"retained after recovery").detail==e.detail,"success cannot overwrite first error");ok(rt->reap_result(r.request),"reap");ok(rt->shutdown(),"shutdown");puts("U10_RUNTIME_FIRST_ERROR_RETAINED_AFTER_RECOVERY_PASS_NOT_BOARD");return 0;}
int unknown(){Fixture f;auto rt=f.start();f.port->unknown=true;value(rt->submit(job()),"unknown-execution request");auto r=rt->run_one();require(!r&&r.error->code==ETIMEDOUT&&r.error->detail=="original unknown execution; do not retry","unknown execution reason retained");require(rt->stats().phase==RuntimePhase::FAULTED&&rt->stats().scheduler.pending==4,"unknown execution retains original reservation");require(!rt->retry_delivery()&&!rt->shutdown()&&f.port->executes==1&&f.port->retires==0,"unknown retirement cannot be guessed");int fd=f.owner->fd();rt.reset();f.owner.reset();require(f.closes==0&&fcntl(fd,F_GETFD)>=0,"unresolved destruction pins owner until process exit");puts("U10_RUNTIME_UNKNOWN_EXECUTION_NEVER_RETRY_OR_CLOSE_PASS_NOT_BOARD");return 0;}
int offline_cycles(){Fixture f;auto rt=f.start();f.port->offline_cycles=true;value(rt->submit(job()),"offline cycle request");auto r=rt->run_one();require(!r&&r.error->code==EPROTO&&!rt->take_result(job().request),"host protocol cannot publish physical GPU cycles");require(rt->stats().phase==RuntimePhase::FAULTED&&f.port->executes==1,"counter scope error quarantines execution");puts("U10_RUNTIME_OFFLINE_PHYSICAL_CYCLES_REJECTED_PASS_NOT_BOARD");return 0;}
int error_evidence(){Fixture f;auto rt=f.start();f.port->unknown=true;value(rt->submit(job()),"unknown evidence request");auto e=rt->run_one();require(!e&&e.error->code==ETIMEDOUT,"return original execution error");require(f.error_logs==1&&f.saved_error&&f.saved_error->execution_error,"unknown execution must persist error before returning");auto&s=*f.saved_error;require(s.execution_error->detail=="original unknown execution; do not retry"&&s.linux_detail&&s.linux_detail->sequence==81&&s.linux_detail->cookie==901&&s.linux_detail->submit_ns==1234,"exact original error and partial snapshot retained");require(!s.outcome.retired&&!s.outcome.acknowledged&&s.execute_end_ns>=s.execute_begin_ns,"partial snapshot cannot imply retirement");require(!rt->retry_delivery()&&!rt->shutdown()&&f.port->executes==1&&f.port->retires==0,"durable failure still quarantined");puts("U10_RUNTIME_UNKNOWN_ERROR_PARTIAL_SNAPSHOT_DURABLE_PASS_NOT_BOARD");return 0;}
int error_log_retry(){Fixture f;auto rt=f.start();f.port->unknown=true;f.fail_work=true;value(rt->submit(job()),"failed log request");auto e=rt->run_one();require(!e&&e.error->code==ETIMEDOUT,"logging error cannot hide original timeout");require(f.error_logs==1&&rt->stats().phase==RuntimePhase::DELIVERY_BLOCKED,"failed error log must be explicitly retryable");require(value(rt->first_error(),"first execution failure").code==ETIMEDOUT,"first execution error retained over ENOSPC");f.fail_work=false;auto retry=rt->retry_delivery();require(!retry&&retry.error->code==ETIMEDOUT&&f.error_logs==2&&f.port->executes==1,"retry evidence only and report original failure");require(f.saved_error->evidence_error&&f.saved_error->evidence_error->code==ENOSPC,"failed persistence evidence retained too");require(rt->stats().phase==RuntimePhase::FAULTED&&rt->stats().scheduler.pending==4&&f.port->retires==0&&!rt->shutdown(),"successful error logging never frees unknown job");puts("U10_RUNTIME_FAILURE_LOG_RETRY_NEVER_EXECUTES_OR_RETIRES_PASS_NOT_BOARD");return 0;}
int exception_evidence(){Fixture f;auto rt=f.start();f.port->unknown=true;f.port->throw_execute=true;value(rt->submit(job()),"exception request");auto failed=rt->run_one();require(!failed&&failed.error->code==EIO&&failed.error->detail=="original executor exception 901"&&f.error_logs==1&&f.saved_error,"executor exception must preserve original reason and snapshot");require(f.saved_error->execute_end_ns>=f.saved_error->execute_begin_ns&&f.saved_error->linux_detail->sequence==81&&!rt->shutdown(),"exception end timing and unretired snapshot retained");puts("U10_RUNTIME_EXECUTION_EXCEPTION_ORIGINAL_EVIDENCE_PASS_NOT_BOARD");return 0;}
int binding(){Fixture f;auto rt=f.start();f.check_binding=true;auto r=job();value(rt->submit(r),"binding admission");ok(rt->run_one(),"complete full binding");ok(rt->reap_result(r.request),"reap");ok(rt->shutdown(),"shutdown");puts("U10_RUNTIME_FULL_REQUEST_HARDWARE_MODEL_SOURCE_BINDING_PASS_NOT_BOARD");return 0;}
template<class T,class=void>struct RuntimeStageChecks {
 static int run(){require(false,"U20 native output staging must bind the active final owner callback");return 1;}
};
template<class T>struct RuntimeStageChecks<T,std::void_t<decltype(std::declval<T&>().stage_display(std::declval<PublishedResult>(),std::declval<PersistResult>()))>> {
 static int run(){
  Fixture f;std::shared_ptr<T>rt=f.start();auto store=rt->trusted_results();require(bool(store),"same common result store available only to trusted builder");auto request=job();request.source=std::make_shared<Source>(2,WorkKind::CPU);
  PublishedResult dummy;auto outside=rt->stage_display(dummy,[](const PublishedResult&){return Result<void>::success();});require(!outside&&outside.error->code==EPERM,"no pre-admission output staging or caller-chosen callback thread");
  unsigned stages=0;std::optional<ConsumerLease>front;f.port->output_hook=[&](const WorkItem&w,ExecutionReceipt&r){
   auto p=*r.output;p.lease.lease_tokens.clear();const auto legacy=p.raw_legacy_json;PersistResult journal=[&,legacy](const PublishedResult&q){++stages;require(q.binding&&q.binding->device_generation==1&&q.binding->source==request.source->identity()&&q.raw_legacy_json==legacy&&!q.gpu_raw_cycles,"private stage has actual admitted binding and original bytes");return Result<void>::success();};
   if(w.work_index==0){auto early=rt->stage_display(p,journal);require(!early&&early.error->code==EPROTO&&!store->stats().records,"prefix cannot stage a final output");r.output.reset();return Result<void>::success();}
   std::atomic<bool>refused{false};std::thread alien([&]{auto x=rt->stage_display(p,journal);refused=!x&&x.error->code==EPERM;});alien.join();require(refused,"other thread cannot stage even during active owner callback");
   auto altered=p;altered.lease.model=digest(77);auto bad=rt->stage_display(altered,journal);require(!bad&&bad.error->code==EPROTO,"staged producer must match admitted model/input/plan/epoch");
   ok(rt->stage_display(p,journal),"final owner callback stages checked output");require(!rt->take_result(w.request)&&store->stats().staged_records==1,"active output never appears as a public result");
   front=value(store->retain_result(w.request,Consumer::DP),"same common private DP source hold");DisplayReceipt shown;shown.cookie=front->token;shown.presented=true;shown.clock_domain=vxnn::ClockDomain::HOST_MONOTONIC;ok(store->mark_displayed(*front,shown),"host protocol presentation marker only");
   r.output=p;f.fail_output=true;return Result<void>::success();
  };
  value(rt->submit(request),"admit two real Runtime CPU operations");ok(rt->run_one(),"nonfinal work runs without publishing");auto blocked=rt->run_one();require(!blocked&&blocked.error->code==EIO&&rt->stats().phase==RuntimePhase::DELIVERY_BLOCKED&&f.port->executes==2&&!rt->take_result(request.request),"public journal failure keeps displayed stage without rerunning math");
  f.fail_output=false;ok(rt->retry_delivery(),"evidence-only commit retry");auto p=value(rt->take_result(request.request),"exact final public result");require(p.displayed&&p.binding&&p.binding->input==request.input_sha&&f.port->executes==2&&stages==2,"commit preserves actual admitted source, DP marker and one execution");
  ok(rt->reap_result(request.request),"client release before actual front retirement");require(f.port->retires==0,"front retains request backing");ok(rt->release_result(*front,true),"confirmed source front retirement");ok(rt->reap_result(request.request),"forget already retired index");ok(rt->shutdown(),"staged runtime shutdown");require(f.port->retires==1&&rt->stats().input_bytes==0,"only confirmed backing retirement releases input budget");
  puts("U20_FINAL_OWNER_BOUND_STAGING_NO_EARLY_PUBLICATION_OR_EXECUTION_RETRY_PASS_NOT_BOARD");return 0;
 }
};

template<class T,class=void>struct RoleChecks {
 static int run(){require(false,"U20 shared residency must count NN2 separately from media lifetime");return 1;}
};
template<class T>struct RoleChecks<T,std::void_t<decltype(std::declval<T&>().reserve_resident(Digest{},uint64_t{},AppKind::IMAGE))>> {
 static int run(){
  Fixture f;std::shared_ptr<T>rt=f.start();constexpr uint64_t margin=128ULL<<20,shadow=132ULL<<20;
  auto image=value(rt->reserve_resident(digest(90),shadow,AppKind::IMAGE),"image lifetime before allocating132MiB");
  auto a=value(rt->reserve_resident(digest(1),4096),"NN1 independent of image"),b=value(rt->reserve_resident(digest(2),8192),"NN2 independent of image");
  require(rt->stats().resident_models==3&&rt->stats().resident_nn_models==2&&rt->stats().cpu_bytes==margin+shadow+12288,"one CPU budget and three typed resident groups");
  auto third=rt->reserve_resident(digest(3),1);require(!third&&third.error->code==ENOSPC,"third NN still forbidden even with tiny extent");
  auto alias=rt->reserve_resident(digest(90),shadow,AppKind::NN);require(!alias&&alias.error->code==ESTALE,"same digest cannot change lifetime role");
  auto foreign=rt->reserve_resident(digest(91),1,AppKind::P8);require(!foreign&&foreign.error->code==EOPNOTSUPP,"S8 cannot reserve a P8 resident bank");
  require(!rt->reserve_resident(digest(91),1,AppKind(99)),"unknown application role refused");
  auto request=job("typed-image");request.app=AppKind::IMAGE;request.model=digest(90);request.resident_bytes=shadow;
  value(rt->submit(request),"same image group shares lifetime bytes");require(rt->stats().cpu_bytes==margin+shadow+12288+request.cpu_bytes,"active request charged once plus original shared lifetime");
  ok(rt->cancel(request.request),"never-started media cancellation");ok(rt->reap_result(request.request),"cancelled media request retires");
  auto wrong=job("role-alias");wrong.model=digest(90);wrong.resident_bytes=shadow;auto denied=rt->submit(wrong);require(!denied&&denied.error->code==ESTALE,"untyped NN request cannot borrow media reservation");
  std::atomic<bool>stable{true};std::vector<std::thread>threads;for(int n=0;n<4;++n)threads.emplace_back([&]{for(int j=0;j<100;++j){auto held=rt->reserve_resident(digest(90),shadow,AppKind::IMAGE);if(!held||rt->stats().cpu_bytes!=margin+shadow+12288)stable=false;}});for(auto&t:threads)t.join();require(stable&&rt->stats().resident_leases==3,"concurrent media pins share exact bytes without another NN slot");
  a.reset();b.reset();auto large=rt->reserve_resident(digest(91),(1ULL<<30)-margin-shadow+1,AppKind::IMAGE);require(!large&&large.error->code==ENOSPC,"media has no separate CPU budget");image.reset();require(rt->stats().resident_models==0&&rt->stats().cpu_bytes==margin,"all exact role charges released");ok(rt->shutdown(),"typed resident close");puts("U20_TYPED_NN2_MEDIA_LIFETIME_SAME_CPU_BUDGET_PASS_HOST_NOT_GPU gpu_raw_cycles=null");return 0;
 }
};
template<class T,class=void>struct ResidentChecks {
 static int run(){require(false,"U20 resident lifetime must be charged before native session allocation");return 1;}
 static int reentrant(){return run();}
};
template<class T>struct ResidentChecks<T,std::void_t<decltype(std::declval<T&>().reserve_resident(Digest{},uint64_t{}))>> {
 static int reentrant(){
  Fixture f;std::shared_ptr<T>rt=f.start();auto pin=value(rt->reserve_resident(digest(1),1024),"native session lifetime before allocation");auto r=job();
  value(rt->submit(r),"request retaining native session");ok(rt->run_one(),"execute real runtime path");
  f.port->on_retire=[&]{pin.reset();};puts("U20_ENTER_REENTRANT_RESIDENT_RETIRE");fflush(stdout);alarm(3);
  ok(rt->reap_result(r.request),"adapter retirement may destroy last native session lifetime");alarm(0);
  require(rt->stats().resident_leases==0&&rt->stats().resident_models==0&&rt->stats().cpu_bytes==(128ULL<<20),"native session release inside adapter retirement has exact accounting");
  ok(rt->shutdown(),"shutdown after reentrant lifetime destruction");puts("U20_RESIDENT_ADAPTER_RETIRE_DESTRUCTION_NO_LOCK_REENTRY_PASS_NOT_BOARD");return 0;
 }
 static int run(){
  Fixture f;std::shared_ptr<T>rt=f.start();constexpr uint64_t margin=128ULL<<20,shadow=132ULL<<20;
  require(!rt->reserve_resident(Digest{},shadow)&&!rt->reserve_resident(digest(1),0),"resident declaration must have identity and nonzero bytes");
  auto pin=value(rt->reserve_resident(digest(1),shadow),"charge image full lifetime before create");
  require(pin->model()==digest(1)&&pin->bytes()==shadow&&rt->stats().cpu_bytes==margin+shadow&&rt->stats().resident_models==1&&rt->stats().resident_leases==1,"global idle resident132MiB and model2 accounting");
  auto again=value(rt->reserve_resident(digest(1),shadow),"same model shares bytes with another lifetime");
  auto changed=rt->reserve_resident(digest(1),shadow+1);require(!changed&&changed.error->code==ESTALE,"same model cannot change footprint");
  require(rt->stats().cpu_bytes==margin+shadow&&rt->stats().resident_leases==2,"do not double-charge identical residency");
  auto too_big=rt->reserve_resident(digest(2),(1ULL<<30)-margin-shadow+1);require(!too_big&&too_big.error->code==ENOSPC&&rt->stats().resident_models==1,"global allocation failure rolls back model slot");
  auto second=value(rt->reserve_resident(digest(2),600ULL<<20),"second resident model");
  auto third=rt->reserve_resident(digest(3),1);require(!third&&third.error->code==ENOSPC,"idle sessions share global model2");second.reset();
  auto r=job();r.resident_bytes=shadow;value(rt->submit(r),"request borrows charged model");require(rt->stats().cpu_bytes==margin+shadow+r.cpu_bytes,"request cannot double-charge lifetime");
  pin.reset();again.reset();require(rt->stats().resident_leases==0&&rt->stats().resident_models==1,"last pin release cannot release active request residency");
  auto active_pin=value(rt->reserve_resident(digest(1),shadow),"reacquire active model lifetime");ok(rt->run_one(),"real runtime CPU and result flow");ok(rt->reap_result(r.request),"request retirement");
  require(f.port->retires==1&&rt->stats().cpu_bytes==margin+shadow&&rt->stats().resident_models==1,"request retirement preserves idle session shadow");
  using Hold=decltype(active_pin);std::vector<Hold>holds;for(int n=1;n<64;++n)holds.push_back(value(rt->reserve_resident(digest(1),shadow),"bounded resident pin"));
  auto full=rt->reserve_resident(digest(1),shadow);require(!full&&full.error->code==ENOSPC&&rt->stats().resident_leases==64,"lifetime pin count64 globally bounded");holds.clear();
  std::atomic<bool>good{true};std::vector<std::thread>threads;
  for(int n=0;n<4;++n)threads.emplace_back([&]{for(int j=0;j<200;++j){auto x=rt->reserve_resident(digest(1),shadow);if(!x||rt->stats().cpu_bytes!=margin+shadow)good=false;}});
  for(auto&t:threads){t.join();}require(good&&rt->stats().resident_leases==1&&rt->stats().cpu_bytes==margin+shadow,"concurrent shared residency cannot leak or exceed budget");
  active_pin.reset();require(rt->stats().cpu_bytes==margin&&rt->stats().resident_models==0&&rt->stats().resident_leases==0,"last idle lifetime releases exact charge");
  ok(rt->shutdown(),"resident test shutdown");require(!rt->reserve_resident(digest(1),1),"closing runtime rejects new session allocation");
  puts("U20_SHARED_RESIDENT_LIFETIME_GLOBAL_BUDGET_CONCURRENT_PASS_NOT_BOARD");return 0;
 }
};
}

static BackendIdentity joint_identity(){
 BackendIdentity h;h.provider_flags=4;h.device_generation=1;
 auto hex=[](const char*s){Digest d{};for(unsigned i=0;i<32;i++){auto n=[](char c){return unsigned(c<='9'?c-'0':c-'a'+10);};d[i]=(n(s[i*2])<<4)|n(s[i*2+1]);}return d;};
 h.hardware_contract=hex("1bf11d26789e7c602f9c05a5ecc97e2605ac7807deade8f4bfc549487f45ec5d");
 h.bit=hex("94ae9d3e6eb73ba55a8eec8c802e3818cd6b3a9be7e60542da5ae9459e3579e1");
 h.xsa=hex("02e68160e21b01621fde1eac6e472810e0fe50cd1842acb3ab63469314355e94");
 h.compatible=hex("f670c653a37361f05f57c14eecd53dea1b87556489a2c1314efd208d715826dc");return h;
}
static int joint_mode(){
 Fixture f;f.port->bound=joint_identity();f.port->output_models[AppKind::P8]=digest(4);f.port->output_models[AppKind::IMAGE]=digest(5);auto rt=f.start();
 require(rt->mode_allows(AppKind::NN)&&!rt->mode_allows(AppKind::P8),"joint startup explicitly NN mode");
 auto nn=job("joint-nn");value(rt->submit(nn),"joint NN request");
 auto switching=value(rt->request_mode(AppKind::P8),"request P8 mode");require(switching.switching&&switching.current==AppKind::NN,"whole request must drain before mode change");
 require(!rt->submit(job("blocked-old-nn")),"switch closes old mode admission");require(!rt->mode_allows(AppKind::NN)&&!rt->mode_allows(AppKind::P8),"both new admissions closed while draining");
 require(!value(rt->advance_mode(),"mode remains pending"),"queued original request blocks handoff");
 ok(rt->run_one(),"original whole NN request completes");
 auto front=value(rt->retain_result(nn.request,Consumer::DP),"keep previous valid front buffer");
 require(value(rt->advance_mode(),"retired NN permits P8"),"successful handoff");require(rt->mode_allows(AppKind::P8)&&!rt->mode_allows(AppKind::NN),"P8 is the single active mode");
 auto snapshot=rt->mode_snapshot();require(!snapshot.switching&&snapshot.completed_ns>=snapshot.requested_ns&&snapshot.sequence==1,"actual monotonic handoff timing");
 auto p8=job("joint-p8");p8.app=AppKind::P8;p8.model=digest(4);value(rt->submit(p8),"P8 frame");ok(rt->run_one(),"P8 full frame");
 value(rt->request_mode(AppKind::IMAGE),"request image mode");require(value(rt->advance_mode(),"frame boundary image handoff"),"P8 completed frame boundary");
 auto image=job("joint-image");image.app=AppKind::IMAGE;image.model=digest(5);value(rt->submit(image),"image request");ok(rt->run_one(),"image completes");
 value(rt->request_mode(AppKind::NN),"return NN");require(value(rt->advance_mode(),"image boundary NN handoff"),"three-mode roundtrip");require(rt->mode_snapshot().sequence==3,"three mode receipts");
 require(rt->take_result(nn.request)&&rt->trusted_results()->stats().consumer_leases==1,"old DP front remains valid across mode handoff");
 ok(rt->release_result(front,true),"confirmed front retirement");for(auto k:{nn.request,p8.request,image.request})ok(rt->reap_result(k),"reap actual terminal request");
 require(rt->stats().scheduler.requests==0&&rt->stats().scheduler.pending==0&&rt->stats().input_bytes==0,"no residual execution references");ok(rt->shutdown(),"joint shutdown");
 std::puts("S8_JOINT_RUNTIME_WHOLE_REQUEST_HANDOFF_KEEP_DP_FRONT_AND_THREE_MODES_PASS_HOST_ONLY");return 0;
}


static int joint_queued(){
 Fixture f;f.port->bound=joint_identity();auto rt=f.start();std::vector<std::string>order;
 f.port->output_hook=[&](const WorkItem&w,ExecutionReceipt&r){order.push_back(w.request.request_id);if(w.work_index==0)r.output.reset();return Result<void>::success();};
 auto a=job("queued-A"),b=job("queued-B");a.source=std::make_shared<Source>(2,WorkKind::CPU);b.source=std::make_shared<Source>(2,WorkKind::CPU);
 value(rt->submit(a),"first whole request");value(rt->submit(b),"second accepted request must queue without faulting sole worker");
 value(rt->request_mode(AppKind::P8),"mode waits for previously accepted queue");
 for(unsigned i=0;i<4;i++){require(!value(rt->advance_mode(),"queue still draining"),"old queued work blocks mode handoff");ok(rt->run_one(),"whole-request work");}
 require((order==std::vector<std::string>{"queued-A","queued-A","queued-B","queued-B"}),"joint dispatch never alternates jobs across requests");
 require(value(rt->advance_mode(),"accepted old queue retired"),"handoff follows complete old queue");
 ok(rt->reap_result(a.request),"reap A");ok(rt->reap_result(b.request),"reap B");ok(rt->shutdown(),"queue shutdown");puts("JOINT_QUEUE8_WHOLE_REQUEST_BARRIER_AND_MODE_DRAIN_PASS_NOT_BOARD");return 0;
}
static int joint_cancel_drain(){
 Fixture f;f.port->bound=joint_identity();auto rt=f.start();auto a=job("cancelled-before-final");a.source=std::make_shared<Source>(3,WorkKind::CPU);
 f.port->output_hook=[](const WorkItem&,ExecutionReceipt&r){r.output.reset();return Result<void>::success();};
 value(rt->submit(a),"admit cancellation source");ok(rt->run_one(),"first original job retires");ok(rt->cancel(a.request),"cancel remainder");value(rt->request_mode(AppKind::P8),"handoff after cancellation");
 require(value(rt->advance_mode(),"retire cancelled adapter backing"),"mode handoff drains cancelled owner");require(f.port->retires==1&&rt->stats().input_bytes==0,"terminal label alone cannot stand in for adapter retirement");ok(rt->reap_result(a.request),"forget cancelled index");ok(rt->shutdown(),"cancelled mode shutdown");
 puts("JOINT_CANCELLED_ADAPTER_RETIRES_BEFORE_MODE_READY_PASS_NOT_BOARD");return 0;
}
static int joint_closed_reservation(){
 Fixture f;f.port->bound=joint_identity();auto rt=f.start();require(rt->mode_allows(AppKind::NN),"worker checked original mode");
 value(rt->request_mode(AppKind::P8),"control closes old mode between native check and reservation");
 auto late=rt->reserve_preparation({"boot","native","late"});require(!late,"unbound native reservation after closure cannot bypass mode gate");
 require(rt->stats().preparing_requests==0,"closed admission never charges preparation queue");require(value(rt->advance_mode(),"no late work to drain"),"mode changes without stale reservation");ok(rt->shutdown(),"reservation shutdown");puts("JOINT_NATIVE_CHECK_RESERVE_RACE_CLOSED_PASS_NOT_BOARD");return 0;
}


static int joint_cancel_queued(){
 Fixture f;f.port->bound=joint_identity();auto rt=f.start();
 struct WaitForRetire:Source{const std::atomic<int>&retired;explicit WaitForRetire(const std::atomic<int>&r):Source(1,WorkKind::CPU),retired(r){}Result<bool>ready(uint64_t)const override{return Result<bool>::success(retired.load()!=0);}};
 auto a=job("cancelled-owner"),b=job("queued-successor");a.source=std::make_shared<Source>(3,WorkKind::CPU);b.source=std::make_shared<WaitForRetire>(f.port->retires);
 f.port->output_hook=[](const WorkItem&w,ExecutionReceipt&r){if(w.request.request_id=="cancelled-owner")r.output.reset();return Result<void>::success();};
 value(rt->submit(a),"admit owner");value(rt->submit(b),"admit waiting successor");ok(rt->run_one(),"owner nonfinal job retires");ok(rt->cancel(a.request),"cancel remaining owner jobs");value(rt->request_mode(AppKind::P8),"handoff waits for queued successor");
 require(!value(rt->advance_mode(),"queued successor still needs execution"),"mode cannot skip accepted successor");require(f.port->retires==1,"cancelled adapter must retire before waiting for successor queue drain");
 ok(rt->run_one(),"successor becomes ready after original owner retirement without reap");require(value(rt->advance_mode(),"successor and owner now drained"),"handoff completes without client reap prerequisite");
 ok(rt->reap_result(a.request),"forget cancelled owner");ok(rt->reap_result(b.request),"reap completed successor");ok(rt->shutdown(),"cancel/successor shutdown");puts("JOINT_CANCELLED_OWNER_RETIRES_BEFORE_QUEUED_SUCCESSOR_DRAIN_PASS_NOT_BOARD");return 0;
}
static int joint_typed_reservation(){
 Fixture f;f.port->bound=joint_identity();auto rt=f.start();RequestKey key{"boot","native","preclosure"};
 auto slot=value(rt->reserve_preparation(key,AppKind::NN),"typed reservation before mode closure");
 value(rt->request_mode(AppKind::P8),"closure after original native check/reservation");
 require(!rt->reserve_preparation({"boot","native","late-typed"},AppKind::NN),"typed postclosure reservation must fail atomically");
 require(!rt->reserve_preparation({"boot","native","wrong-mode"},AppKind::P8),"target cannot prepare before handoff");
 require(!value(rt->advance_mode(),"original reservation still charged"),"preclosure reservation blocks handoff");
 auto work=job("preclosure");work.request=key;value(rt->submit(work,slot),"original typed preclosure reservation may drain");ok(rt->run_one(),"reserved original work completes");require(value(rt->advance_mode(),"reserved old work drained"),"handoff after original typed admission");ok(rt->reap_result(key),"reap typed original");ok(rt->shutdown(),"typed reservation shutdown");puts("JOINT_TYPED_PRE_CLOSURE_DRAIN_AND_POST_CLOSURE_ATOMIC_REJECTION_PASS_NOT_BOARD");return 0;
}

int main(int argc,char**argv){if(argc!=2)return 2;std::string c=argv[1];if(c=="joint-mode")return joint_mode();if(c=="joint-cancel-queued")return joint_cancel_queued();if(c=="joint-typed-reservation")return joint_typed_reservation();if(c=="joint-queued")return joint_queued();if(c=="joint-cancel-drain")return joint_cancel_drain();if(c=="joint-closed-reservation")return joint_closed_reservation();if(c=="flow")return flow();if(c=="limits")return limits();if(c=="cancel")return cancel_active();if(c=="bad-output")return bad_output();if(c=="late-cancel")return late_cancel();if(c=="expiry")return expiry();if(c=="error")return original_error();if(c=="unknown")return unknown();if(c=="offline-cycles")return offline_cycles();if(c=="binding")return binding();if(c=="exception")return exception_evidence();if(c=="error-evidence")return error_evidence();if(c=="error-log-retry")return error_log_retry();if(c=="stage")return RuntimeStageChecks<Runtime>::run();if(c=="roles")return RoleChecks<Runtime>::run();if(c=="residency")return ResidentChecks<Runtime>::run();if(c=="residency-reentrant")return ResidentChecks<Runtime>::reentrant();return 2;}

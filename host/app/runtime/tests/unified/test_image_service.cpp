#include <vxunified/image_network.hpp>
#include "image/network/image_rx_linux.h"
#include "scheduler_fixture.hpp"
#include "image_owner_bridge.h"
#include <vxl/hunet.h>
#include <fcntl.h>
#include <unistd.h>
#include <fstream>
#include <cstring>
#include <time.h>
#include <atomic>
#include <thread>
namespace {
std::vector<uint8_t>read(const char*path){std::ifstream f(path,std::ios::binary);require(bool(f),"fixture file");return {(std::istreambuf_iterator<char>(f)),{}};}
uint64_t ns(){timespec t{};require(!clock_gettime(CLOCK_MONOTONIC,&t),"clock");return uint64_t(t.tv_sec)*1000000000ULL+t.tv_nsec;}
struct Transport:IoctlTransport{void*bridge;Transport(const char*c,const char*d,Digest compatible):bridge(image_owner_bridge_create(c,d,compatible.data())){}~Transport(){image_owner_bridge_destroy(bridge);}int call(unsigned long c,void*p)override{return image_owner_bridge_call(bridge,c,p);}uint64_t now_ns()override{return ns();}};
}
static std::atomic<int>allocation_fault{-1};
void*operator new(std::size_t n){if(allocation_fault.load()>=0&&allocation_fault.fetch_sub(1)==0){allocation_fault=-1;throw std::bad_alloc();}void*p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();return p;}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}
int main(int argc,char**argv){
 require(argc==5,"catalog/programdir/table/mode");std::string mode=argv[4];char dir[]="/tmp/vx-image-owner-XXXXXX";require(mkdtemp(dir)!=nullptr,"private owner");ServiceConfig config;config.private_dir=dir;auto owner=value(DeviceOwner::acquire(config,[](const std::string&){return Result<int>::success(open("/dev/null",O_RDWR|O_CLOEXEC));}),"single shared fd");std::shared_ptr<ImageWorkRequest>app;Transport*io=nullptr;
 ExecutionHooks hooks;hooks.prepare_gpu=[&](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r){return app->handlers().prepare_gpu(w,b,r);};hooks.finish_gpu=[&](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r){return app->handlers().finish_gpu(w,b,r);};hooks.non_gpu=[&](const WorkItem&w,UnifiedBackend&b,vxnn::CpuPool*p,ExecutionReceipt&r){return app->handlers().non_gpu(w,b,p,r);};hooks.retire_request=[&](RequestKey k,UnifiedBackend&b){return app->handlers().retire_request(k,b);};
 auto port=std::make_shared<ComputeExecutionPort>(owner,hooks,[&](int fd){require(fd==owner->fd(),"borrowed sole fd");auto p=std::make_unique<Transport>(argv[1],argv[2],vxnn::content_digest("vortex,hw4-s8-demo-v1"));io=p.get();return p;});auto hw=value(port->probe(owner->fd(),BackendMode::COMPUTE_V2),"actual C driver fixture caps");auto backend=value(port->adapter_backend(),"shared backend");auto caps=value(backend->capabilities(),"C native capabilities");auto raw=read(argv[1]);Digest sum{};vxl_hunet_hash(raw.data(),raw.size(),sum.data());auto catalog=value(InstalledCatalog::decode({raw.data(),raw.size()},hw,sum),"C validated exact IMAGE catalog");auto residency=value(ProgramResidency::create(catalog,backend),"shared program and data owner");auto table=read(argv[3]);require(table.size()==65536,"frozen table65536");

 if(mode=="oom"){
  const std::string boot="boot-network",bundle="20945ed189bad393bad2fe56a420e789c82c33cd9b790adccca4a115ba0423e3";unsigned refused=0;
  for(int n=0;n<12;n++){
   allocation_fault=n;
   try{auto made=ImageReceiver::create(hw,boot,42,bundle,100,vxnn::EvidenceScope::HOST_PROTOCOL_TEST);allocation_fault=-1;if(made)ok((*made.value)->close(),"successful allocation retires receiver");else{require(made.error->code==ENOMEM,"bounded allocation error");refused++;}}
   catch(const std::bad_alloc&){allocation_fault=-1;refused++;}
   auto clean=ImageReceiver::create(hw,boot,42,bundle,100,vxnn::EvidenceScope::HOST_PROTOCOL_TEST);require(bool(clean),"OOM create must not leave singleton");ok((*clean.value)->close(),"clean owner after OOM");
  }
  require(refused>=3,"actual allocation failures observed");ok(residency->shutdown(),"OOM no resident program");puts("U15_TYPED_IMAGE_SERVICE_RESULT_SHA_DP_RETIREMENT_PASS_HOST_NOT_GPU_DP allocation-failure gpu_raw_cycles=null");return 0;
 }
 const std::string receiver_boot=mode=="acquire-oom"?std::string(64,'b'):"boot-network";
 auto created=ImageReceiver::create(hw,receiver_boot,42,"20945ed189bad393bad2fe56a420e789c82c33cd9b790adccca4a115ba0423e3",100,vxnn::EvidenceScope::HOST_PROTOCOL_TEST);
 require(bool(created),"trusted UDP receiver must bind shared runtime result");auto receiver=*created.value;
 auto session=value(ImageSession::create(catalog,caps,residency,digest(7),digest(8),{table.data(),table.size()},vxnn::EvidenceScope::HOST_PROTOCOL_TEST),"image session");
 std::vector<uint8_t>rgba(IMAGE_BYTES);for(size_t i=0;i<rgba.size();i++)rgba[i]=i%4==3?255:uint8_t(i%251);
 std::vector<uint8_t>reply;uint64_t tick=1;ImagePeer peer{11,22};
 auto send=[&](image_header q,const uint8_t*p,size_t n){std::vector<uint8_t>packet(64+n);q.length=n;q.payload_crc=n?image_crc32({p,n}):0;image_header_encode(packet.data(),&q);if(n)std::memcpy(packet.data()+64,p,n);reply=value(receiver->handle({packet.data(),packet.size()},peer,tick++),"wire receiver");image_header h{};require(!image_header_decode(&h,reply.data(),reply.size()),"original header");return h;};
 image_header q{};q.op=IMAGE_BEGIN;q.session=42;q.tx=1;q.input_id=1;q.style=1;q.preset=1;q.width=1280;q.height=720;q.image_crc=image_crc32({rgba.data(),rgba.size()});uint8_t rect[16]{};image_put32(rect+8,1280);image_put32(rect+12,720);require(send(q,rect,16).state==IMAGE_RECEIVING,"receiving");q.op=IMAGE_DATA;for(size_t i=0;i<rgba.size();i+=1200){q.offset=i;send(q,rgba.data()+i,std::min(size_t(1200),rgba.size()-i));}
 if(mode=="acquire-oom"){
  unsigned refused=0;
  for(int n=0;n<16;n++){
   allocation_fault=n;
   try{auto taken=receiver->acquire();allocation_fault=-1;if(taken){require(bool(*taken.value),"ready ingress acquired under allocation probe");ok(receiver->fail((*taken.value)->serial,6),"cancel unadmitted allocation probe");}else{require(taken.error->code==ENOMEM,"bounded acquire allocation error");refused++;}}
   catch(const std::bad_alloc&){allocation_fault=-1;refused++;}
   require(bool(receiver->close()),"acquire OOM must not leave pinned request");
   if(n!=15){receiver=value(ImageReceiver::create(hw,std::string(64,'b'),42,"20945ed189bad393bad2fe56a420e789c82c33cd9b790adccca4a115ba0423e3",100,vxnn::EvidenceScope::HOST_PROTOCOL_TEST),"next independent receiver");q.op=IMAGE_BEGIN;q.offset=0;require(send(q,rect,16).state==IMAGE_RECEIVING,"next independent image");q.op=IMAGE_DATA;for(size_t i=0;i<rgba.size();i+=1200){q.offset=i;send(q,rgba.data()+i,std::min(size_t(1200),rgba.size()-i));}}
  }
  require(refused>=4,"actual immutable snapshot allocation failures");ok(residency->shutdown(),"no native program started during ingress OOM");puts("U15_TYPED_IMAGE_SERVICE_RESULT_SHA_DP_RETIREMENT_PASS_HOST_NOT_GPU_DP acquire-allocation-failure gpu_raw_cycles=null");return 0;
 }
 auto ingress=value(receiver->acquire(),"acquire immutable image");require(bool(ingress)&&ingress->image.rgba&&*ingress->image.rgba==rgba&&ingress->image.cookie_namespace==0,"immutable full input, no namespace allocated at ingress");require(!value(receiver->acquire(),"pinned second acquire"),"single pending");ingress->image.cookie_namespace=2;
 std::optional<ImagePresentation>presentation;unsigned traces=0;
 auto trace=[&](const RequestKey&,const ImageJobTrace&t){value(encode_image_job_trace(t),"raw trace");if(!t.error)traces++;return Result<void>::success();};
 auto output=[&](const WorkRequest&w,vxnn::Bytes bytes,const ImageOwnerReceipt&r){require(r.render_begin_ns&&r.render_end_ns>=r.render_begin_ns,"render times before presentation");ImagePresentation p;p.display.cookie=ingress->image.display_cookie;p.display.clock_domain=vxnn::ClockDomain::HOST_MONOTONIC;p.display.submitted_ns=10;p.display.presented_ns=20;p.display.had_old_front=true;p.display.old_retired=true;p.display.old_retired_ns=30;p.display.presented=true;p.output.execution_complete=true;p.output.lease.producer=w.request;p.output.lease.app=AppKind::IMAGE;p.output.lease.scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;p.output.lease.hardware=hw.hardware_contract;p.output.lease.model=w.model;p.output.lease.plan=w.plan;ResultArtifact a;a.path="host-marker.rgba";a.layout="RGBA8888";a.shape={720,1280,4};a.bytes=bytes.size;vxl_hunet_hash(bytes.data,bytes.size,a.sha.data());p.output.lease.artifacts={a};if(mode=="initial-blank")p.display.had_old_front=false;presentation=p;return Result<ImagePresentation>::success(p);};
 if(mode=="wrong-style")ingress->image.params={4,2,4};
 app=value(session->prepare(ingress->image,60000000000ULL,trace,output),"deferred shared owner");
 auto runtime=value(Runtime::start(owner,hw,vxnn::EvidenceScope::HOST_PROTOCOL_TEST,port,[](const ExecutionReceipt&,DeliveryStage){return Result<void>::success();},[](const PublishedResult&){return Result<void>::success();}),"common runtime");
 auto accepted=value(runtime->submit(app->request()),"common admission");auto wrong=accepted;wrong.request_id="foreign";require(!receiver->admitted(ingress->serial,app,wrong,0),"foreign admission rejected");if(mode=="wrong-style"){require(!receiver->admitted(ingress->serial,app,accepted,0),"wrong style admission must refuse");ok(runtime->cancel(accepted),"cancel unstarted mismatched factory work");ok(runtime->reap_result(accepted),"retire cancelled source");ok(receiver->fail(ingress->serial,6),"original protocol refusal");ok(receiver->close(),"close failed transaction");ok(runtime->shutdown(),"shutdown");ok(residency->shutdown(),"release");puts("U15_TYPED_IMAGE_SERVICE_RESULT_SHA_DP_RETIREMENT_PASS_HOST_NOT_GPU_DP wrong-style gpu_raw_cycles=null");return 0;}
 ok(receiver->admitted(ingress->serial,app,accepted,0),"bind exact admitted request");require(!receiver->admitted(ingress->serial,app,accepted,0),"double admission refused");
 require(!receiver->complete(ResultLease{},DisplayReceipt{},1),"no APPLIED before real result");q.op=IMAGE_STATUS;require(send(q,nullptr,0).state==IMAGE_PROCESSING,"still processing");
 std::atomic<bool>stop{false};std::atomic<unsigned>snapshots{0};
 std::thread query([&](){while(!stop.load()){require(!receiver->complete(ResultLease{},DisplayReceipt{},1),"concurrent incomplete proof cannot complete");snapshots++;}});
 auto query_deadline=ns()+1000000000ULL;while(!snapshots.load()){require(ns()<query_deadline,"snapshot thread starts");std::this_thread::yield();}
 for(uint64_t i=0;i<app->size();i++){ok(runtime->run_one(),"actual C owner run");}
 stop=true;query.join();require(snapshots>0,"concurrent typed receipt snapshots");
 require(traces==5&&presentation&&app->receipt().retired,"ACKed5 and retired");auto lease=value(runtime->take_result(accepted),"actual common ResultLease").lease;auto bad=lease;bad.producer.request_id="stale";require(!receiver->complete(bad,presentation->display,UINT64_C(60000000000)),"stale result rejected");bad=lease;bad.artifacts[0].sha[0]^=1;require(!receiver->complete(bad,presentation->display,UINT64_C(60000000000)),"wrong output hash rejected");bad=lease;bad.hardware[0]^=1;require(!receiver->complete(bad,presentation->display,UINT64_C(60000000000)),"foreign hardware rejected");auto dp=presentation->display;dp.cookie++;require(!receiver->complete(lease,dp,UINT64_C(60000000000)),"wrong KMS cookie rejected");dp=presentation->display;dp.old_retired=false;require(!receiver->complete(lease,dp,UINT64_C(60000000000)),"old front still scanned rejected");
 require(send(q,nullptr,0).state==IMAGE_PROCESSING,"bad proof cannot report APPLIED");ok(receiver->complete(lease,presentation->display,UINT64_C(60000000000)),"typed completed+displayed+retired result");auto done=send(q,nullptr,0);require(done.state==IMAGE_APPLIED&&reply.size()==128&&done.detail==app->receipt().image_result->output_crc,"original metrics only after all proofs");require(image_get32(reply.data()+64+12*4)==1000000000u,"Linux monotonic ns frequency");require(!receiver->complete(lease,presentation->display,UINT64_C(60000000000)),"completed transaction cannot replay");ok(runtime->reap_result(accepted),"retire common result");ok(receiver->close(),"receiver close");ok(runtime->shutdown(),"runtime shutdown");ok(residency->shutdown(),"program retirement");require(!image_owner_bridge_live(io->bridge),"no live backing");puts("U15_TYPED_IMAGE_SERVICE_RESULT_SHA_DP_RETIREMENT_PASS_HOST_NOT_GPU_DP gpu_raw_cycles=null");
}

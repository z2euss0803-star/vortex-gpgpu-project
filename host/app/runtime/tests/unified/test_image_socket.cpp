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
#include <sys/socket.h>
#include <arpa/inet.h>
#include <poll.h>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <chrono>
namespace {
std::vector<uint8_t>read(const char*path){std::ifstream f(path,std::ios::binary);require(bool(f),"fixture file");return {(std::istreambuf_iterator<char>(f)),{}};}
uint64_t ns(){timespec t{};require(!clock_gettime(CLOCK_MONOTONIC,&t),"clock");return uint64_t(t.tv_sec)*1000000000ULL+t.tv_nsec;}
struct Transport:IoctlTransport{void*bridge;Transport(const char*c,const char*d,Digest compatible):bridge(image_owner_bridge_create(c,d,compatible.data())){}~Transport(){image_owner_bridge_destroy(bridge);}int call(unsigned long c,void*p)override{return image_owner_bridge_call(bridge,c,p);}uint64_t now_ns()override{return ns();}};
}
int main(int argc,char**argv){
 require(argc==5,"catalog/programdir/table/mode");std::string mode=argv[4];char dir[]="/tmp/vx-image-owner-XXXXXX";require(mkdtemp(dir)!=nullptr,"private owner");ServiceConfig config;config.private_dir=dir;auto owner=value(DeviceOwner::acquire(config,[](const std::string&){return Result<int>::success(open("/dev/null",O_RDWR|O_CLOEXEC));}),"single shared fd");std::shared_ptr<ImageWorkRequest>app;Transport*io=nullptr;
 ExecutionHooks hooks;hooks.prepare_gpu=[&](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r){return app->handlers().prepare_gpu(w,b,r);};hooks.finish_gpu=[&](const WorkItem&w,UnifiedBackend&b,ExecutionReceipt&r){return app->handlers().finish_gpu(w,b,r);};hooks.non_gpu=[&](const WorkItem&w,UnifiedBackend&b,vxnn::CpuPool*p,ExecutionReceipt&r){return app->handlers().non_gpu(w,b,p,r);};hooks.retire_request=[&](RequestKey k,UnifiedBackend&b){return app->handlers().retire_request(k,b);};
 auto port=std::make_shared<ComputeExecutionPort>(owner,hooks,[&](int fd){require(fd==owner->fd(),"borrowed sole fd");auto p=std::make_unique<Transport>(argv[1],argv[2],vxnn::content_digest("vortex,hw4-s8-demo-v1"));io=p.get();return p;});auto hw=value(port->probe(owner->fd(),BackendMode::COMPUTE_V2),"actual C driver fixture caps");auto backend=value(port->adapter_backend(),"shared backend");auto caps=value(backend->capabilities(),"C native capabilities");auto raw=read(argv[1]);Digest sum{};vxl_hunet_hash(raw.data(),raw.size(),sum.data());auto catalog=value(InstalledCatalog::decode({raw.data(),raw.size()},hw,sum),"C validated exact IMAGE catalog");auto residency=value(ProgramResidency::create(catalog,backend),"shared program and data owner");auto table=read(argv[3]);require(table.size()==65536,"frozen table65536");


 auto receiver=value(ImageReceiver::create(hw,"boot-socket",42,"20945ed189bad393bad2fe56a420e789c82c33cd9b790adccca4a115ba0423e3",100,vxnn::EvidenceScope::HOST_PROTOCOL_TEST),"typed socket receiver");auto session=value(ImageSession::create(catalog,caps,residency,digest(7),digest(8),{table.data(),table.size()},vxnn::EvidenceScope::HOST_PROTOCOL_TEST),"image session");
 auto runtime=value(Runtime::start(owner,hw,vxnn::EvidenceScope::HOST_PROTOCOL_TEST,port,[](const ExecutionReceipt&,DeliveryStage){return Result<void>::success();},[](const PublishedResult&){return Result<void>::success();}),"one runtime");
 int fd=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0);require(fd>=0,"loopback socket");sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=0;require(!bind(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address)),"private loopback bind");socklen_t addrlen=sizeof(address);require(!getsockname(fd,reinterpret_cast<sockaddr*>(&address),&addrlen),"port");printf("PORT %u\n",unsigned(ntohs(address.sin_port)));fflush(stdout);
 bool drop_data=false,drop_first=false,drop_last=false;uint32_t jobs=0,space=0,transactions=0;auto until=ns()+UINT64_C(120000000000);
 std::mutex queue_mutex;std::condition_variable changed;bool stopping=false;std::optional<ImageIngress>queued;
 std::thread executor([&](){
  for(;;){
   std::optional<ImageIngress>ingress;
   {std::unique_lock<std::mutex>lock(queue_mutex);changed.wait(lock,[&](){return stopping||queued.has_value();});
    if(stopping){if(queued){auto serial=queued->serial;queued.reset();lock.unlock();ok(receiver->fail(serial,3),"cancel unstarted receiver on host stop");}break;}
    ingress=std::move(queued);queued.reset();
   }
auto runtimemarker=ns();
  printf("HOST_OWNER_BEGIN transaction=%u ns=%llu\n",transactions+1,(unsigned long long)runtimemarker);fflush(stdout);
  if(!transactions&&std::getenv("VX_IMAGE_RX_TEST_DELAY_MS")){require(std::string(std::getenv("VX_IMAGE_RX_TEST_DELAY_MS"))=="12000","bounded test-only owner pause");std::this_thread::sleep_for(std::chrono::milliseconds(12000));}
 ingress->image.cookie_namespace=++space;unsigned traces=0;std::optional<ImagePresentation>presentation;
  auto trace=[&](const RequestKey&,const ImageJobTrace&t){value(encode_image_job_trace(t),"full raw host trace");require(!t.gpu_raw_cycles,"no physical GPU clock");if(!t.error)traces++;return Result<void>::success();};
  auto output=[&](const WorkRequest&w,vxnn::Bytes bytes,const ImageOwnerReceipt&r){require(r.render_begin_ns&&r.render_end_ns>=r.render_begin_ns,"owner render interval");ImagePresentation p;p.display.cookie=ingress->image.display_cookie;p.display.clock_domain=vxnn::ClockDomain::HOST_MONOTONIC;p.display.submitted_ns=ns();p.display.presented_ns=ns();p.display.presented=true;p.output.execution_complete=true;p.output.lease.producer=w.request;p.output.lease.app=AppKind::IMAGE;p.output.lease.scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;p.output.lease.hardware=hw.hardware_contract;p.output.lease.model=w.model;p.output.lease.plan=w.plan;ResultArtifact a;a.path="host-marker.rgba";a.layout="RGBA8888";a.shape={720,1280,4};a.bytes=bytes.size;vxl_hunet_hash(bytes.data,bytes.size,a.sha.data());p.output.lease.artifacts={a};presentation=p;return Result<ImagePresentation>::success(p);};
  app=value(session->prepare(ingress->image,60000000000ULL,trace,output),"actual owner prepare");auto admitted=value(runtime->submit(app->request()),"common runtime submit");ok(receiver->admitted(ingress->serial,app,admitted,jobs),"typed admission");for(uint64_t i=0;i<app->size();i++){ok(runtime->run_one(),"actual C owner work");}require(presentation&&traces==app->plan().total_jobs,"all raw completion traces");auto result=value(runtime->take_result(admitted),"common result");ok(receiver->complete(result.lease,presentation->display,ns()-runtimemarker),"socket trusted completion");jobs+=traces;ok(runtime->reap_result(admitted),"result retirement");transactions++;printf("HOST_TRANSACTION %u jobs=%u cycles=null\n",transactions,traces);fflush(stdout);
  }
  ok(runtime->shutdown(),"runtime closed by sole executor");ok(residency->shutdown(),"residency closed by sole executor");
 });
 for(unsigned packets=0;packets<20000;packets++){
  require(ns()<until,"bounded socket lifetime");pollfd wait{fd,POLLIN,0};int ready=poll(&wait,1,5000);require(ready==1,"socket no unbounded wait");uint8_t packet[1280];sockaddr_in peer{};socklen_t peerlen=sizeof(peer);ssize_t n=recvfrom(fd,packet,sizeof(packet),0,reinterpret_cast<sockaddr*>(&peer),&peerlen);require(n>0,"recv");if(n==4&&!std::memcmp(packet,"STOP",4))break;
  image_header h{};if(image_header_decode(&h,packet,size_t(n)))continue;if(h.op==IMAGE_DATA&&h.offset==1200&&!drop_data){drop_data=true;continue;}
  auto reply=value(receiver->handle({packet,size_t(n)},{peer.sin_addr.s_addr,ntohs(peer.sin_port)},ns()/1000000),"original typed handle");bool dropped=false;if(h.op==IMAGE_DATA&&h.offset==0&&!drop_first){drop_first=true;dropped=true;}if(h.op==IMAGE_DATA&&h.offset+h.length==IMAGE_BYTES&&!drop_last){drop_last=true;dropped=true;}if(!dropped&&!reply.empty())require(sendto(fd,reply.data(),reply.size(),0,reinterpret_cast<sockaddr*>(&peer),peerlen)==ssize_t(reply.size()),"reply");
  auto incoming=value(receiver->acquire(),"socket acquire");
  if(incoming){std::lock_guard<std::mutex>lock(queue_mutex);require(!queued,"one bounded pending image");queued=std::move(incoming);changed.notify_one();}

 }
 {std::lock_guard<std::mutex>lock(queue_mutex);stopping=true;}changed.notify_one();executor.join();
 require(transactions==4&&jobs==7&&drop_data&&drop_first&&drop_last,"four original/cache/pixel transactions with three controlled packet losses");ok(receiver->close(),"socket receiver closes");require(!image_owner_bridge_live(io->bridge),"executor retired all backing");close(fd);puts("U15_ORIGINAL_PC_SENDER_SOCKET_TYPED_RUNTIME_PASS_HOST_MARKER_NOT_GPU_DP gpu_raw_cycles=null");
}

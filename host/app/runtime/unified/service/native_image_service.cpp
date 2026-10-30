#include <vxunified/native_image_service.hpp>
#include <vxunified/journal.hpp>
#include <vxunified/exit_retention.hpp>
#include "image/network/image_rx_linux.h"
#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>
#include <atomic>
#include <mutex>
#include <stdexcept>
namespace vxunified {namespace {
vxnn::Error error(int code,const char*detail){return {code,"NATIVE_IMAGE_SERVICE",detail};}
uint64_t clock_ns(){timespec t{};if(clock_gettime(CLOCK_MONOTONIC,&t))throw std::runtime_error("native ingress monotonic clock");return uint64_t(t.tv_sec)*1000000000+t.tv_nsec;}
struct Socket{int fd=-1;~Socket(){if(fd>=0)::close(fd);}void close(){if(fd>=0){::close(fd);fd=-1;}}};
}
struct NativeImageService::State {
 detail::ExitRetention retention;
 StartupContext ctx;std::shared_ptr<NativeImageLibrary>library;std::shared_ptr<ImageReceiver>receiver;
 Socket socket;uint16_t port=0;uint32_t session=0,jobs=0;std::atomic<bool>closing{false};
 std::mutex io_mutex,work_mutex,metrics_mutex;bool bound=false,released=false;
 NativeImageServiceStats metrics,wire_baseline;uint32_t wire_tx=0;uint64_t wire_begin=0;
 struct Active{ImageIngress input;NativeImagePrepared prepared;NativeImageServiceStats baseline;uint64_t begin=0;};
 std::unique_ptr<Active>active;
};
NativeImageService::NativeImageService(std::shared_ptr<State>s):state_(std::move(s)){}
NativeImageService::~NativeImageService(){try{if(quiesce()&&release())return;}catch(...){}state_->retention.retain(state_);}
Result<std::shared_ptr<NativeImageService>>NativeImageService::create(const StartupContext&c,std::shared_ptr<NativeImageLibrary>library,uint32_t session,const std::string&bundle,uint64_t display_base,uint16_t port,const std::string&address){
 using R=Result<std::shared_ptr<NativeImageService>>;try{
  auto admitted=admit_native_startup(c,1);if(!admitted)return R::failure(*admitted.error);auto rt=c.runtime.lock();
  sockaddr_in endpoint{};endpoint.sin_family=AF_INET;endpoint.sin_port=htons(port);
  if(!library||!library->owns_runtime(rt.get())||!c.notify_work||inet_pton(AF_INET,address.c_str(),&endpoint.sin_addr)!=1)return R::failure(error(EXDEV,"same native library/worker and numeric IPv4 bind required"));
  auto s=std::make_shared<State>();s->ctx=c;s->library=std::move(library);s->session=session;
  auto receiver=ImageReceiver::create_native(c,session,bundle,display_base);if(!receiver)return R::failure(*receiver.error);s->receiver=*receiver.value;
  s->socket.fd=::socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);if(s->socket.fd<0)return R::failure(error(errno,"create native UDP socket"));
  if(::bind(s->socket.fd,reinterpret_cast<sockaddr*>(&endpoint),sizeof(endpoint)))return R::failure(error(errno,"exclusive native UDP bind"));
  socklen_t size=sizeof(endpoint);if(getsockname(s->socket.fd,reinterpret_cast<sockaddr*>(&endpoint),&size))return R::failure(error(errno,"read actual UDP endpoint"));s->port=ntohs(endpoint.sin_port);
  return R::success(std::shared_ptr<NativeImageService>(new NativeImageService(s)));
 }catch(const std::bad_alloc&){return R::failure(error(ENOMEM,"native service startup allocation"));}catch(const std::exception&e){return R::failure({EIO,"NATIVE_IMAGE_SERVICE",e.what()});}
}
Result<void>NativeImageService::bind(ApplicationSet&set){
 try{auto s=state_;std::lock_guard<std::mutex>lock(s->work_mutex);if(s->bound||s->closing||set.poll_native||set.prepare_native)return Result<void>::failure(error(EBUSY,"one trusted ingress binding"));
  auto self=shared_from_this();auto before_stop=set.quiesce,before_release=set.release_resources;
  auto stop=[self,before_stop](){auto r=self->quiesce();if(!r)return r;return before_stop?before_stop():Result<void>::success();};
  auto release=[self,before_release](){auto r=self->release();if(!r)return r;return before_release?before_release():Result<void>::success();};
  std::function<Result<void>(uint32_t)>poller=[self](uint32_t timeout){return self->poll(timeout);};std::function<Result<bool>()>prepare=[self](){return self->step();};std::function<Result<void>()>stopper=stop,releaser=release;
  set.poll_native=std::move(poller);set.prepare_native=std::move(prepare);set.quiesce=std::move(stopper);set.release_resources=std::move(releaser);s->bound=true;return Result<void>::success();
 }catch(const std::bad_alloc&){return Result<void>::failure(error(ENOMEM,"native service hook allocation"));}
}
Result<void>NativeImageService::poll(uint32_t timeout){
 if(auto rt=state_->ctx.runtime.lock())if(!rt->mode_allows(AppKind::IMAGE))return Result<void>::success();
 try{auto s=state_;std::lock_guard<std::mutex>lock(s->io_mutex);if(s->closing)return Result<void>::success();
  pollfd fd{s->socket.fd,POLLIN,0};int ready=::poll(&fd,1,int(std::min(timeout,uint32_t(50))));if(ready<0&&errno==EINTR)return Result<void>::success();if(ready<0||fd.revents&(POLLERR|POLLHUP|POLLNVAL))return Result<void>::failure(error(ready<0?errno:EIO,"native UDP poll"));
  bool notify=false;
  for(unsigned window=0;ready&&window<8;++window){
   uint8_t packet[1280];sockaddr_in peer{};socklen_t size=sizeof(peer);auto n=recvfrom(s->socket.fd,packet,sizeof(packet),MSG_TRUNC,reinterpret_cast<sockaddr*>(&peer),&size);
   if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK)){break;}
   if(n<0&&errno==EINTR){continue;}
   if(n<0){return Result<void>::failure(error(errno,"native UDP receive"));}
   if(n>ssize_t(sizeof(packet))){continue;}
   auto begin=clock_ns();auto reply=s->receiver->handle({packet,size_t(n)},{peer.sin_addr.s_addr,ntohs(peer.sin_port)},begin/1000000);if(!reply)return Result<void>::failure(*reply.error);auto elapsed=clock_ns()-begin;
   image_header q{},r{};bool valid_reply=reply.value->size()>=64&&!image_header_decode(&r,reply.value->data(),reply.value->size());bool start=!image_header_decode(&q,packet,size_t(n))&&(q.op==IMAGE_BEGIN||q.op==IMAGE_STYLE)&&q.session==s->session&&valid_reply&&r.state!=IMAGE_FAILED&&r.detail!=5;
   {std::lock_guard<std::mutex>metrics(s->metrics_mutex);if(start&&q.tx!=s->wire_tx){s->wire_tx=q.tx;s->wire_begin=begin;s->wire_baseline=s->metrics;}++s->metrics.packets;s->metrics.wire_bytes+=size_t(n);s->metrics.protocol_ns+=elapsed;}
   if(!reply.value->empty()){auto sent=sendto(s->socket.fd,reply.value->data(),reply.value->size(),0,reinterpret_cast<sockaddr*>(&peer),size);if(sent<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK)return Result<void>::failure(error(errno,"native UDP reply"));if(sent>=0&&size_t(sent)!=reply.value->size())return Result<void>::failure(error(EIO,"short native UDP reply"));}
   notify=notify||(valid_reply&&r.state==IMAGE_READY);
  }
  auto progress=s->receiver->poll(clock_ns()/1000000);if(!progress)return progress;if(notify){auto wake=s->ctx.notify_work();if(!wake)return wake;}return Result<void>::success();
 }catch(const std::bad_alloc&){return Result<void>::failure(error(ENOMEM,"bounded native UDP metadata"));}catch(const std::exception&e){return Result<void>::failure({EIO,"NATIVE_IMAGE_SERVICE",e.what()});}
}
Result<bool>NativeImageService::step(){
 using R=Result<bool>;try{auto s=state_;std::lock_guard<std::mutex>lock(s->work_mutex);if(s->closing)return R::success(false);auto rt=s->ctx.runtime.lock();auto journal=s->ctx.journal.lock();if(!rt||!journal)return R::failure(error(ESHUTDOWN,"native service common roots expired"));
  if(s->active){auto&a=*s->active;auto begin=clock_ns();auto status=rt->status(a.input.image.request);if(!status)return R::failure(*status.error);if(*status.value!=RequestStatus::COMPLETED){if(*status.value==RequestStatus::FAILED||*status.value==RequestStatus::CANCELLED)return R::failure(error(EIO,"native image request did not complete"));return R::success(false);}
   auto result=rt->take_result(a.input.image.request);if(!result)return R::failure(*result.error);auto receipt=a.prepared.owner->receipt_snapshot();if(!receipt)return R::failure(*receipt.error);if(!receipt.value->display)return R::failure(error(EPROTO,"native completion missing exact display receipt"));
   NativeImageServiceStats measured;{std::lock_guard<std::mutex>metrics(s->metrics_mutex);measured=s->metrics;}IngressTiming timing;timing.serial=a.input.serial;timing.begin_ns=a.begin;timing.end_ns=clock_ns();timing.packets=measured.packets-a.baseline.packets;timing.bytes=measured.wire_bytes-a.baseline.wire_bytes;timing.protocol_ns=measured.protocol_ns-a.baseline.protocol_ns;timing.prepare_ns=measured.prepare_ns-a.baseline.prepare_ns;timing.complete_ns=timing.end_ns-begin;
   // Durable precommit timing is explicitly separate from the final original
   // protocol commit. No client sees APPLIED before required evidence is saved.
   if(receipt.value->completed_jobs>UINT32_MAX-s->jobs){return R::failure(error(EOVERFLOW,"native cumulative job count exhausted"));}
   if(result.value->gpu_raw_cycles&&measured.gpu_raw_cycles&&*result.value->gpu_raw_cycles>UINT64_MAX-*measured.gpu_raw_cycles){return R::failure(error(EOVERFLOW,"native cumulative raw cycle count exhausted"));}
   auto saved=journal->save_ingress(a.input.image.request,s->ctx.identity,s->ctx.scope,timing);if(!saved)return R::failure(*saved.error);
   auto done=s->receiver->complete(result.value->lease,*receipt.value->display,clock_ns()-a.begin);if(!done)return R::failure(*done.error);
   s->jobs+=uint32_t(receipt.value->completed_jobs);auto reaped=rt->reap_result(a.input.image.request);if(!reaped)return R::failure(*reaped.error);
   {std::lock_guard<std::mutex>metrics(s->metrics_mutex);s->metrics.complete_ns+=clock_ns()-begin;++s->metrics.completed;if(result.value->gpu_raw_cycles)s->metrics.gpu_raw_cycles=s->metrics.gpu_raw_cycles.value_or(0)+*result.value->gpu_raw_cycles;}s->active.reset();return R::success(true);
  }
  if(!rt->mode_allows(AppKind::IMAGE))return R::success(false);
  auto ingress=s->receiver->acquire();if(!ingress)return R::failure(*ingress.error);if(!*ingress.value)return R::success(false);auto begin=clock_ns();auto incoming=**ingress.value;
  auto prepared=s->library->prepare(incoming.image);if(!prepared){(void)s->receiver->fail(incoming.serial,6);return R::failure(*prepared.error);}
  auto active=std::make_unique<State::Active>();active->input=incoming;active->prepared=*prepared.value;{std::lock_guard<std::mutex>metrics(s->metrics_mutex);active->begin=s->wire_begin?s->wire_begin:begin;active->baseline=s->wire_baseline;}
  PreparationReceipt evidence;evidence.request=incoming.image.request;evidence.hardware=s->ctx.identity;evidence.model=prepared.value->work.model;evidence.plan=prepared.value->work.plan;evidence.input=prepared.value->work.input_sha;evidence.source=prepared.value->work.source->identity();evidence.entry="image-udp";evidence.input_id=std::to_string(incoming.image.input_id);evidence.begin_ns=begin;evidence.end_ns=clock_ns();auto saved=journal->save_preparation(evidence,s->ctx.scope);if(!saved){(void)s->receiver->fail(incoming.serial,6);return R::failure(*saved.error);}
  auto accepted=rt->submit(prepared.value->work,incoming.preparation);if(!accepted){(void)s->receiver->fail(incoming.serial,6);return R::failure(*accepted.error);}s->active=std::move(active);auto bound=s->receiver->admitted(incoming.serial,prepared.value->owner,*accepted.value,s->jobs);if(!bound)return R::failure(*bound.error);
  {std::lock_guard<std::mutex>metrics(s->metrics_mutex);s->metrics.prepare_ns+=clock_ns()-begin;}return R::success(true);
 }catch(const std::bad_alloc&){return R::failure(error(ENOMEM,"native worker metadata allocation; backing retained"));}catch(const std::exception&e){return R::failure({EIO,"NATIVE_IMAGE_SERVICE",e.what()});}
}
Result<void>NativeImageService::quiesce(){auto s=state_;s->closing=true;std::lock_guard<std::mutex>lock(s->io_mutex);s->socket.close();return Result<void>::success();}
Result<void>NativeImageService::release(){
 auto s=state_;std::lock_guard<std::mutex>lock(s->work_mutex);if(s->released)return Result<void>::success();if(!s->closing)return Result<void>::failure(error(EBUSY,"ingress must quiesce before release"));
 if(s->active){auto receipt=s->active->prepared.owner->receipt_snapshot();if(!receipt)return Result<void>::failure(*receipt.error);if(!receipt.value->retired||receipt.value->first_error||receipt.value->evidence_error)return Result<void>::failure(error(EBUSY,"native owner retirement unproven; original evidence retained"));auto cancelled=s->receiver->fail(s->active->input.serial,3);if(!cancelled)return cancelled;s->active.reset();}
 auto closed=s->receiver->close();if(!closed)return closed;s->library.reset();s->released=true;return Result<void>::success();
}
uint16_t NativeImageService::port()const{return state_->port;}
NativeImageServiceStats NativeImageService::stats()const{auto s=state_;std::lock_guard<std::mutex>worker(s->work_mutex);std::lock_guard<std::mutex>lock(s->metrics_mutex);return s->metrics;}
}

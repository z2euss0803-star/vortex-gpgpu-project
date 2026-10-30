#include <vxunified/service.hpp>
#include "nnrt/package/json.hpp"
#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
namespace vxunified {
namespace {
namespace json=vxnn::json;
constexpr size_t max_frame=65536;
struct LocalFd {int value=-1;~LocalFd(){if(value>=0)close(value);}};
bool id(const std::string&s){return !s.empty()&&s.size()<=128&&std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'||c==':';});}
uint64_t now_ms(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
Result<void> wait_fd(int fd,short events,uint64_t deadline){
 for(;;){uint64_t now=now_ms();if(now>=deadline)return Result<void>::failure({ETIMEDOUT,"RPC_PEER","bounded peer deadline"});pollfd p{fd,events,0};int r=poll(&p,1,int(deadline-now));if(r<0&&errno==EINTR)continue;if(r<0)return Result<void>::failure({errno,"RPC_PEER","poll peer"});if(!r)return Result<void>::failure({ETIMEDOUT,"RPC_PEER","bounded peer deadline"});if(p.revents&events)return Result<void>::success();return Result<void>::failure({ECONNRESET,"RPC_PEER","peer disconnected"});}
}
Result<void> transfer(int fd,void*data,size_t bytes,bool sending,uint64_t deadline){
 auto*p=static_cast<char*>(data);size_t done=0;while(done<bytes){auto w=wait_fd(fd,sending?POLLOUT:POLLIN,deadline);if(!w)return w;ssize_t n=sending?send(fd,p+done,bytes-done,MSG_NOSIGNAL):recv(fd,p+done,bytes-done,0);if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))continue;if(n<=0)return Result<void>::failure({n?errno:ECONNRESET,"RPC_PEER","truncated/disconnected frame"});done+=size_t(n);}return Result<void>::success();
}
Result<RpcRequest> decode(const std::string&raw,const std::string&boot){
 try{
  auto tree=json::parse(raw);auto*o=tree.get();auto command=json::str(json::member(o,"command"));json::check(json::u32(json::member(o,"version"))==1,"RPC version");RpcRequest r;r.command=command;
  if(command=="models"||command=="scenes"||command=="effects"){json::fields(o,{"version","command"});return Result<RpcRequest>::success(r);}
  if(command=="inputs"||command=="results"){
   json::fields(o,{"version","command","boot_id","entry_id"});r.request.boot_id=json::str(json::member(o,"boot_id"));r.entry_id=json::str(json::member(o,"entry_id"));json::check(id(r.request.boot_id)&&id(r.entry_id),"bounded boot and model entry required");if(r.request.boot_id!=boot)return Result<RpcRequest>::failure({ESTALE,"RPC","boot epoch differs"});return Result<RpcRequest>::success(std::move(r));
  }
  if(command=="mode"||command=="mode-status"){
   if(command=="mode")json::fields(o,{"version","command","boot_id","mode"});else json::fields(o,{"version","command","boot_id"});
   r.request.boot_id=json::str(json::member(o,"boot_id"));
   if(r.request.boot_id!=boot)return Result<RpcRequest>::failure({ESTALE,"RPC","boot epoch differs"});
   if(command=="mode"){r.entry_id=json::str(json::member(o,"mode"));json::check(r.entry_id=="nn"||r.entry_id=="image"||r.entry_id=="p8","one registered joint mode required");}
   return Result<RpcRequest>::success(std::move(r));
  }
  if(command=="submit"||command=="input-register")json::fields(o,{"version","command","boot_id","client_id","request_id","entry_id","input_id","parameters"});
  else if(command=="display")json::fields(o,{"version","command","boot_id","client_id","request_id","artifact"});
  else if(command=="export")json::fields(o,{"version","command","boot_id","client_id","request_id","artifact","destination_id"});
  else {json::check(command=="status"||command=="cancel"||command=="result"||command=="reap"||command=="recover","unknown command");json::fields(o,{"version","command","boot_id","client_id","request_id"});}
  r.request={json::str(json::member(o,"boot_id")),json::str(json::member(o,"client_id")),json::str(json::member(o,"request_id"))};json::check(id(r.request.boot_id)&&id(r.request.client_id)&&id(r.request.request_id),"invalid request key");
  if(r.request.boot_id!=boot)return Result<RpcRequest>::failure({ESTALE,"RPC","boot epoch differs"});
  if(command=="submit"||command=="input-register"){
   r.entry_id=json::str(json::member(o,"entry_id"));r.input_id=json::str(json::member(o,"input_id"));json::check(id(r.entry_id)&&id(r.input_id),"entry and input are registry identifiers, not paths");
   auto*params=json::member(o,"parameters");json::check(params&&json_object_is_type(params,json_type_object)&&json_object_object_length(params)<=16,"parameter map limit16");
   json_object_object_foreach(params,k,v){json::check(id(k),"parameter name");auto type=json_object_get_type(v);json::check(type==json_type_boolean||type==json_type_int||(type==json_type_string&&json::str(v).size()<=128),"only bounded scalar parameters");}
   r.parameters=json::dump(params);
  }
  if(command=="display"||command=="export"){r.artifact=json::u32(json::member(o,"artifact"));json::check(r.artifact<64,"artifact index");}
  if(command=="export"){r.destination_id=json::str(json::member(o,"destination_id"));json::check(id(r.destination_id),"destination must be registered identifier");}
  return Result<RpcRequest>::success(std::move(r));
 }catch(const std::bad_alloc&){return Result<RpcRequest>::failure({ENOMEM,"RPC","decode allocation"});}catch(const std::exception&e){return Result<RpcRequest>::failure({EINVAL,"RPC",e.what()});}
}
std::string reply(const Result<std::string>&r,const std::string&boot){
 json::Ptr o(json_object_new_object(),json_object_put);json::check(bool(o),"reply allocation");json_object_object_add(o.get(),"version",json_object_new_int(1));json_object_object_add(o.get(),"boot_id",json_object_new_string(boot.c_str()));json_object_object_add(o.get(),"ok",json_object_new_boolean(bool(r)));
 if(r){auto parsed=json::parse(*r.value);json_object_object_add(o.get(),"result",json_object_get(parsed.get()));}
 else {auto*e=json_object_new_object();json_object_object_add(e,"code",json_object_new_int(r.error->code));json_object_object_add(e,"category",json_object_new_string(r.error->category.c_str()));json_object_object_add(e,"detail",json_object_new_string(r.error->detail.c_str()));json_object_object_add(o.get(),"error",e);}
 return json::dump(o.get());
}
}
struct RpcServer::State {
 int listen_fd=-1;std::string path,boot;dev_t dev=0;ino_t ino=0;RpcHandler handler;
 ~State(){if(listen_fd>=0)close(listen_fd);if(ino){struct stat st{};if(lstat(path.c_str(),&st)==0&&S_ISSOCK(st.st_mode)&&st.st_dev==dev&&st.st_ino==ino)unlink(path.c_str());}}
};
RpcServer::RpcServer(std::unique_ptr<State>s):state_(std::move(s)){}
RpcServer::~RpcServer()=default;
Result<std::shared_ptr<RpcServer>> RpcServer::start(const std::string&dir,const std::string&boot,RpcHandler h){
 try{
  if(!id(boot)||!h||dir.empty()||dir[0]!='/'||dir.find('\0')!=std::string::npos)return Result<std::shared_ptr<RpcServer>>::failure({EINVAL,"RPC","private directory/boot/handler required"});
  LocalFd directory;directory.value=open(dir.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);if(directory.value<0)return Result<std::shared_ptr<RpcServer>>::failure({errno,"RPC","open private socket directory"});struct stat st{};if(fstat(directory.value,&st)<0)return Result<std::shared_ptr<RpcServer>>::failure({errno,"RPC","stat socket directory"});if(st.st_uid!=geteuid()||(st.st_mode&07777)!=0700)return Result<std::shared_ptr<RpcServer>>::failure({EACCES,"RPC","socket directory must be same uid0700"});
  auto s=std::make_unique<State>();s->path=dir+"/control.sock";s->boot=boot;s->handler=std::move(h);sockaddr_un address{};address.sun_family=AF_UNIX;if(s->path.size()>=sizeof(address.sun_path))return Result<std::shared_ptr<RpcServer>>::failure({ENAMETOOLONG,"RPC","socket path limit"});memcpy(address.sun_path,s->path.c_str(),s->path.size()+1);
  s->listen_fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);if(s->listen_fd<0)return Result<std::shared_ptr<RpcServer>>::failure({errno,"RPC","create control socket"});
  if(bind(s->listen_fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))<0)return Result<std::shared_ptr<RpcServer>>::failure({errno==EADDRINUSE?EBUSY:errno,"RPC","bind without deleting preexisting socket"});
  if(lstat(s->path.c_str(),&st)<0){return Result<std::shared_ptr<RpcServer>>::failure({errno,"RPC","stat bound socket"});}
  s->dev=st.st_dev;s->ino=st.st_ino;
  if(chmod(s->path.c_str(),0600)<0||listen(s->listen_fd,8)<0)return Result<std::shared_ptr<RpcServer>>::failure({errno,"RPC","private0600 listen backlog8"});
  return Result<std::shared_ptr<RpcServer>>::success(std::shared_ptr<RpcServer>(new RpcServer(std::move(s))));
 }catch(const std::bad_alloc&){return Result<std::shared_ptr<RpcServer>>::failure({ENOMEM,"RPC","server allocation"});}catch(...){return Result<std::shared_ptr<RpcServer>>::failure({EIO,"RPC","server startup exception"});}
}
Result<void>RpcServer::serve_if_ready(uint32_t timeout){
 if(!timeout||timeout>2000)return Result<void>::failure({EINVAL,"RPC","timeout outside1..2000ms"});
 pollfd ready{state_->listen_fd,POLLIN,0};int rc=::poll(&ready,1,0);
 if(rc<0&&errno==EINTR)return Result<void>::success();
 if(rc<0||ready.revents&(POLLERR|POLLHUP|POLLNVAL))return Result<void>::failure({rc<0?errno:EIO,"RPC","poll common control listener"});
 return rc?serve_one(timeout):Result<void>::success();
}
Result<void> RpcServer::serve_one(uint32_t timeout){
 if(!timeout||timeout>2000)return Result<void>::failure({EINVAL,"RPC","timeout outside1..2000ms"});
 auto&s=*state_;auto w=wait_fd(s.listen_fd,POLLIN,now_ms()+timeout);if(!w)return w;LocalFd peer;peer.value=accept4(s.listen_fd,nullptr,nullptr,SOCK_CLOEXEC|SOCK_NONBLOCK);if(peer.value<0)return Result<void>::failure({errno,"RPC_PEER","accept peer"});
 ucred cred{};socklen_t size=sizeof(cred);if(getsockopt(peer.value,SOL_SOCKET,SO_PEERCRED,&cred,&size)<0||size!=sizeof(cred)||cred.uid!=geteuid())return Result<void>::failure({EACCES,"RPC_PEER","peer uid differs"});
 const uint64_t deadline=now_ms()+timeout;uint32_t frame=0;auto head=transfer(peer.value,&frame,4,false,deadline);if(!head)return head;size_t n=ntohl(frame);if(!n||n>max_frame)return Result<void>::failure({EMSGSIZE,"RPC_PEER","frame outside1..65536"});
 try{
  std::string raw(n,'\0');auto body=transfer(peer.value,&raw[0],n,false,deadline);if(!body)return body;auto request=decode(raw,s.boot);
  auto invoke=[&]()->Result<std::string>{if(!request)return Result<std::string>::failure(*request.error);try{return s.handler(*request.value);}catch(const std::bad_alloc&){return Result<std::string>::failure({ENOMEM,"RPC","control allocation"});}catch(...){return Result<std::string>::failure({EIO,"RPC","control exception; no automatic retry"});}};
  auto outcome=invoke();std::string response;
  try{response=reply(outcome,s.boot);if(response.size()>max_frame)response=reply(Result<std::string>::failure({EMSGSIZE,"RPC","bounded response exceeded; request outcome retained"}),s.boot);}catch(...){response=reply(Result<std::string>::failure({EIO,"RPC","invalid handler response; request outcome retained"}),s.boot);}
  // A bounded, trusted CPU handler may outlast the framed transport window.
  // Keep the original request I/O bound, then start a separate reply I/O bound
  // after processing. This never extends a GPU job's execution watchdog.
  const uint64_t reply_deadline=now_ms()+timeout;
  frame=htonl(uint32_t(response.size()));auto sent=transfer(peer.value,&frame,4,true,reply_deadline);if(!sent)return sent;return transfer(peer.value,&response[0],response.size(),true,reply_deadline);
 }catch(const std::bad_alloc&){return Result<void>::failure({ENOMEM,"RPC_PEER","bounded frame allocation"});}
}
}

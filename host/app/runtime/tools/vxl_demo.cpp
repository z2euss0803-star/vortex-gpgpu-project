/* Persistent local-only RPC; one accepted worker survives client disconnect. */
#include <vxl/demo_runner.h>
#include <vxl/demo_release.h>
#include <vxl/hunet.h>
#include <json-c/json.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <memory>
#include <future>
#include <chrono>
#include <dirent.h>
#include <algorithm>

namespace {
const char *actions[]={"identity","select-app","stage","run-mosaic","run-hunet","status","display","export"};
bool allowed(const std::string &name){for(auto action:actions)if(name==action)return true;return false;}
std::string response(int error,const char *state,const char *scope="OFFLINE_ADAPTERS_NOT_CONNECTED",const char *asset_sha=nullptr){
 auto *o=json_object_new_object();
 json_object_object_add(o,"schema",json_object_new_int(1));
 json_object_object_add(o,"ok",json_object_new_boolean(error==0));
 json_object_object_add(o,"error",json_object_new_int(error));
 json_object_object_add(o,"state",json_object_new_string(state));
 json_object_object_add(o,"scope",json_object_new_string(scope));
 json_object_object_add(o,"raw_cycles",nullptr);
 if(asset_sha)json_object_object_add(o,"asset_sha256",json_object_new_string(asset_sha));
 std::string out=json_object_to_json_string_ext(o,JSON_C_TO_STRING_PLAIN);json_object_put(o);return out;
}
struct Service {bool configured=false;std::future<int> worker;std::string pending_id,pending_json;int last_error=0;};
const char *control_scope(){
#ifdef DEMO_SOFTWARE_MODEL_TEST
 return "OFFLINE";
#else
 return "REAL_DRIVER_CONTROL_NOT_COMPUTE_RESULT";
#endif
}
std::string control_reply(int rc,const char *state,const demo_session *s,const demo_result *r=nullptr){
 auto *o=json_tokener_parse(response(rc,state,control_scope()).c_str());
 if(s){json_object_object_add(o,"generation",json_object_new_uint64(s->generation));if(s->identity_bound){json_object_object_add(o,"boot_id",json_object_new_string(s->boot_id));json_object_object_add(o,"build_id",json_object_new_string(s->build_id));}}
 if(r){json_object_object_add(o,"result",json_tokener_parse(r->run.json));json_object_object_add(o,"run_id",json_object_new_string(r->run.run_id));json_object_object_add(o,"source_result_sha",json_object_new_string(r->source_result_sha));if(r->run.cycle_valid)json_object_object_add(o,"raw_cycles",json_object_new_uint64(r->run.raw_cycles));}
 std::string out=json_object_to_json_string_ext(o,JSON_C_TO_STRING_PLAIN);json_object_put(o);return out;
}
int stored_result(demo_store *,const char *,demo_result *);
std::string export_manifest(demo_session *s,json_object *request){
 auto *value=json_object_object_get(request,"run_id");const char *id=json_object_get_string(value);
 if(!value||!json_object_is_type(value,json_type_string)||json_object_get_string_len(value)!=32)return control_reply(-EINVAL,"EXPORT_REFUSED",s);
 for(unsigned i=0;i<32;i++)if(!((id[i]>='0'&&id[i]<='9')||(id[i]>='a'&&id[i]<='f')))return control_reply(-EINVAL,"EXPORT_REFUSED",s);
 struct Fd {int fd;~Fd(){if(fd>=0)close(fd);}};
 try{
  Fd directory{openat(s->store->dir_fd,id,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC)};if(directory.fd<0)throw -errno;
  struct stat st{};if(fstat(directory.fd,&st))throw -errno;if(st.st_uid!=geteuid()||(st.st_mode&0022))throw -EPERM;
  DIR *raw=fdopendir(dup(directory.fd));if(!raw)throw -errno;
  std::unique_ptr<DIR,int(*)(DIR*)> entries(raw,closedir);std::vector<std::string> names;
  errno=0;while(auto *entry=readdir(entries.get())){
   std::string name=entry->d_name;if(name=="."||name=="..")continue;
   if(name.empty()||name.size()>128||name.find("..")!=std::string::npos||name=="download-receipt.json"||name=="remote-export-manifest.json"||name.find(".download-")==0)throw -EINVAL;
   for(char c:name)if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='.'||c=='_'))throw -EINVAL;
   names.push_back(name);if(names.size()>512)throw -EFBIG;errno=0;
  }
  if(errno)throw -errno;
  if(names.empty())throw -ENODATA;
  std::sort(names.begin(),names.end());
  auto *obj=json_tokener_parse(control_reply(0,"EXPORT_MANIFEST_READY",s).c_str());
  std::unique_ptr<json_object,decltype(&json_object_put)> result(obj,json_object_put);auto *files=json_object_new_array();json_object_object_add(obj,"files",files);json_object_object_add(obj,"run_id",json_object_new_string(id));
  std::unique_ptr<demo_result> completed(new demo_result{});
  if(!stored_result(s->store,id,completed.get())){json_object_object_add(obj,"result",json_tokener_parse(completed->run.json));json_object_object_add(obj,"source_result_sha",json_object_new_string(completed->source_result_sha));}
  uint64_t total=0;
  for(const auto &name:names){
   Fd file{openat(directory.fd,name.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC)};if(file.fd<0)throw -errno;
   struct stat before{},after{};if(fstat(file.fd,&before))throw -errno;
   if(!S_ISREG(before.st_mode)||before.st_uid!=geteuid()||(before.st_mode&0022)||before.st_size<0||uint64_t(before.st_size)>DEMO_MAX_ASSET)throw -EPERM;
   total+=uint64_t(before.st_size);if(total>512u*1024u*1024u)throw -EFBIG;
   vxl_hunet_sha hash;vxl_hunet_sha_init(&hash);uint8_t buffer[16384],digest[32];uint64_t count=0;
   for(;;){ssize_t n=read(file.fd,buffer,sizeof(buffer));if(n<0&&errno==EINTR)continue;if(n<0)throw -errno;if(!n)break;count+=uint64_t(n);if(count>uint64_t(before.st_size))throw -ESTALE;vxl_hunet_sha_update(&hash,buffer,size_t(n));}
   if(fstat(file.fd,&after))throw -errno;
   if(count!=uint64_t(before.st_size)||before.st_size!=after.st_size||before.st_mtim.tv_sec!=after.st_mtim.tv_sec||before.st_mtim.tv_nsec!=after.st_mtim.tv_nsec||before.st_ctim.tv_sec!=after.st_ctim.tv_sec||before.st_ctim.tv_nsec!=after.st_ctim.tv_nsec)throw -ESTALE;
   vxl_hunet_sha_final(&hash,digest);char hex[65];for(unsigned i=0;i<32;i++)snprintf(hex+i*2,3,"%02x",digest[i]);
   auto *row=json_object_new_object();json_object_object_add(row,"name",json_object_new_string(name.c_str()));json_object_object_add(row,"bytes",json_object_new_uint64(count));json_object_object_add(row,"sha256",json_object_new_string(hex));json_object_array_add(files,row);
  }
  return json_object_to_json_string_ext(obj,JSON_C_TO_STRING_PLAIN);
 }catch(int error){return control_reply(error,"EXPORT_REFUSED",s);}catch(...){return control_reply(-ENOMEM,"EXPORT_REFUSED",s);}
}
int stored_result(demo_store *store,const char *id,demo_result *r){std::unique_ptr<char[]> data(new char[DEMO_MAX_JSON+1]);size_t n=0;int rc=demo_store_read_result(store,id,data.get(),DEMO_MAX_JSON+1,&n);if(!rc)rc=demo_parse_run(data.get(),n,&r->run);if(!rc&&(r->run.state!=DEMO_RESULT_VALIDATED||strcmp(id,r->run.run_id)))rc=-ESTALE;if(!rc)demo_sha_hex(data.get(),n,r->source_result_sha);return rc;}
bool same_request(const std::string &one,const std::string &two){auto *a=json_tokener_parse(one.c_str()),*b=json_tokener_parse(two.c_str());bool same=a&&b&&json_object_equal(a,b);json_object_put(a);json_object_put(b);return same;}
int unavailable_owner(void *){return -ENOSYS;}
int unavailable_start(void *,int,const demo_run *,uint64_t *){return -ENOSYS;}
int unavailable_poll(void *,int,uint64_t){return -ENOSYS;}
bool read_message(int fd,std::string &text){
 char buffer[4096];text.clear();
 while(text.size()<=DEMO_MAX_JSON){
  ssize_t n=read(fd,buffer,sizeof(buffer));
  if(n<0&&errno==EINTR)continue;
  if(n<=0)return false;
  text.append(buffer,static_cast<size_t>(n));
  auto end=text.find('\n');
  if(end!=std::string::npos){if(end!=text.size()-1||end>DEMO_MAX_JSON)return false;text.resize(end);return true;}
 }
 return false;
}
bool write_message(int fd,const std::string &text){
 std::string bytes=text+"\n";size_t offset=0;
 while(offset<bytes.size()){
  ssize_t n=send(fd,bytes.data()+offset,bytes.size()-offset,MSG_NOSIGNAL);
  if(n<0&&errno==EINTR)continue;
  if(n<=0)return false;
  offset+=static_cast<size_t>(n);
 }
 return true;
}
bool address(const std::string &path,sockaddr_un &out){
 if(path.empty()||path.size()>=sizeof(out.sun_path))return false;
 memset(&out,0,sizeof(out));out.sun_family=AF_UNIX;memcpy(out.sun_path,path.c_str(),path.size()+1);return true;
}
std::string dispatch(demo_session *session,Service &service,const std::string &message){
 bool running=service.worker.valid()&&service.worker.wait_for(std::chrono::seconds(0))!=std::future_status::ready;
 if(service.worker.valid()&&!running)service.last_error=service.worker.get();
 json_tokener *tok=json_tokener_new_ex(32);
 json_tokener_set_flags(tok,JSON_TOKENER_STRICT|JSON_TOKENER_VALIDATE_UTF8);
 json_object *o=json_tokener_parse_ex(tok,message.data(),static_cast<int>(message.size()));
 bool valid=json_tokener_get_error(tok)==json_tokener_success&&json_tokener_get_parse_end(tok)==message.size()&&o&&json_object_is_type(o,json_type_object);
 json_tokener_free(tok);
 if(!valid){json_object_put(o);return response(-EINVAL,"INVALID_REQUEST");}
 json_object *a=nullptr,*request=nullptr;
 json_object_object_get_ex(o,"action",&a);json_object_object_get_ex(o,"request",&request);
 if(!a||!json_object_is_type(a,json_type_string)||!request||!json_object_is_type(request,json_type_object)){json_object_put(o);return response(-EINVAL,"INVALID_REQUEST");}
 std::string action=json_object_get_string(a),reply;
 if(!allowed(action))reply=response(-EINVAL,"UNKNOWN_ACTION");
 else if(action=="identity")reply=running?control_reply(0,"RUNNING",nullptr):service.configured?control_reply(0,session->quarantined?"QUARANTINED":"READY_TO_SELECT",session):response(0,"ADAPTERS_NOT_CONNECTED");
 else if(running&&action!="status"&&action!="run-mosaic"&&action!="run-hunet")reply=control_reply(-EBUSY,"RUNNING",nullptr);
 else if(action=="select-app"){
  const char *app=json_object_get_string(json_object_object_get(request,"app"));
  int rc=!service.configured?-ENODATA:!app?-EINVAL:!strcmp(app,"mosaic")?demo_select_app(session,DEMO_MOSAIC):!strcmp(app,"hunet")?demo_select_app(session,DEMO_HUNET):-EINVAL;
  reply=control_reply(rc,rc?"SELECT_REFUSED":"APP_SELECTED",session);
 }
 else if(action=="stage"){
  demo_asset asset{};
  const char *payload=json_object_to_json_string_ext(request,JSON_C_TO_STRING_PLAIN);
  int rc=demo_parse_asset(payload,strlen(payload),&asset);
  if(!rc){
   int directory=openat(session->store->dir_fd,"incoming",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
   if(directory<0)rc=-errno;
   else {
    std::string name=std::string(asset.sha256)+".part";
    int source=openat(directory,name.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW);close(directory);
    if(source<0)rc=-errno;else {rc=demo_stage(session->store,&asset,source);close(source);}
   }
  }
  reply=response(rc,rc?"ASSET_REJECTED":"ASSET_STAGED","OFFLINE_ADAPTERS_NOT_CONNECTED",rc?nullptr:asset.sha256);
 }else if(action=="run-mosaic"||action=="run-hunet"){
  std::unique_ptr<demo_run> run(new demo_run{});
  const char *payload=json_object_to_json_string_ext(request,JSON_C_TO_STRING_PLAIN);
  int rc=demo_parse_run(payload,strlen(payload),run.get());
  if(!rc&&((action=="run-mosaic"&&run->app!=DEMO_MOSAIC)||(action=="run-hunet"&&run->app!=DEMO_HUNET)))rc=-EINVAL;
  if(rc)reply=response(rc,"INVALID_RUN");
  else if(!service.configured)reply=response(-ENOSYS,"ADAPTERS_NOT_CONNECTED");
  else if(running)reply=control_reply(service.pending_id==run->run_id&&same_request(service.pending_json,run->json)?0:-EBUSY,"RUNNING",nullptr);
  else if(!strcmp(session->request.run_id,run->run_id)&&session->state==DEMO_RESULT_VALIDATED){
   if(!same_request(session->request.json,run->json))reply=control_reply(-EEXIST,"RUN_ID_CONFLICT",session);
   else reply=control_reply(0,"RESULT_VALIDATED",session,&session->result);
  }else{
   rc=demo_enqueue(session,run.get());
   if(rc)reply=control_reply(rc,session->quarantined?"QUARANTINED":"RUN_REFUSED",session);
   else {
    service.pending_id=run->run_id;service.pending_json=run->json;service.last_error=0;
    std::shared_ptr<demo_run> accepted(run.release());
    try{service.worker=std::async(std::launch::async,[session,accepted](){
     int err=0;try{err=demo_start(session,accepted.get());if(!err){std::unique_ptr<demo_result> out(new demo_result{});err=demo_status(session,accepted->run_id,out.get());}}catch(...){err=-EIO;}
     if(err){session->quarantined=1;session->state=DEMO_QUARANTINED;(void)demo_event(session->store,accepted->run_id,"QUARANTINED");}return err;
    });reply=control_reply(0,"QUEUED",nullptr);}
    catch(...){session->quarantined=1;session->state=DEMO_QUARANTINED;(void)demo_event(session->store,accepted->run_id,"QUARANTINED");reply=control_reply(-EAGAIN,"QUARANTINED",session);}
   }
  }
 }else if(action=="status"){
  json_object *id=nullptr;json_object_object_get_ex(request,"run_id",&id);
  if(!id||!json_object_is_type(id,json_type_string))reply=response(-EINVAL,"RUN_ID_REQUIRED");
  else {
   const char *run_id=json_object_get_string(id);
   if(running)reply=control_reply(service.pending_id==run_id?0:-EBUSY,"RUNNING",nullptr);
   else {
    std::unique_ptr<demo_result> result(new demo_result{});
    if(service.pending_id==run_id&&service.last_error)reply=control_reply(service.last_error,"QUARANTINED",session);
    else {int rc=demo_status(session,run_id,result.get());if(rc==-ENOENT)rc=stored_result(session->store,run_id,result.get());reply=control_reply(rc,session->quarantined?"QUARANTINED":!rc&&result->run.state==DEMO_RESULT_VALIDATED?"RESULT_VALIDATED":"NO_RUNTIME_RESULT",session,!rc&&result->run.state==DEMO_RESULT_VALIDATED?result.get():nullptr);}
   }
  }
 }else if(action=="export")reply=export_manifest(session,request);
 else if(action=="display"){
  auto *id=json_object_object_get(request,"run_id");std::unique_ptr<demo_result> result(new demo_result{});
  int rc=!id||!json_object_is_type(id,json_type_string)?-EINVAL:stored_result(session->store,json_object_get_string(id),result.get());
  if(!rc&&action=="display"){
   const char *sha=json_object_get_string(json_object_object_get(request,"source_result_sha")),*composition=json_object_get_string(json_object_object_get(request,"composition"));
   demo_layout layout=!composition||!strcmp(composition,"RESULT")?DEMO_LAYOUT_RESULT:!strcmp(composition,"INPUT")?DEMO_LAYOUT_INPUT:!strcmp(composition,"COMPARE")?DEMO_LAYOUT_COMPARE:demo_layout(0);
   rc=!sha||strcmp(sha,result->source_result_sha)?-ESTALE:!layout?-EINVAL:demo_request_display(session,result->run.run_id,layout);
  }
  reply=control_reply(rc,rc?"RESULT_ACTION_REFUSED":"DISPLAY_REQUEST_COMPLETED",session,!rc?result.get():nullptr);
 }else reply=response(-ENOSYS,"ADAPTERS_NOT_CONNECTED");
 json_object_put(o);return reply;
}
struct SessionDelete {void operator()(demo_session *s)const{int rc=demo_release_uninstall(s);if(rc){fprintf(stderr,"provider shutdown retained faulted session %d\n",rc);return;}delete s;}};
int serve(const std::string &store_path,const std::string &socket_path,const std::string &runtime_root){
 demo_store store{};demo_gpu_ops ops{};sockaddr_un addr{};int rc,lock_fd,listener;
 std::unique_ptr<demo_session,SessionDelete> session(new demo_session{});
 /* The future waits before session/provider teardown on every error return. */
 Service service;
 if(!address(socket_path,addr))return 2;
 rc=demo_store_open(&store,store_path.c_str(),nullptr);if(rc){fprintf(stderr,"store error %d\n",rc);return 1;}
 if(mkdirat(store.dir_fd,"incoming",0700)&&errno!=EEXIST)return 1;
 lock_fd=openat(store.dir_fd,"runner.lock",O_CREAT|O_RDWR|O_NOFOLLOW|O_CLOEXEC,0600);
 if(lock_fd<0||flock(lock_fd,LOCK_EX|LOCK_NB)){fprintf(stderr,"runner owner busy\n");return 1;}
 ops.open_owner=unavailable_owner;ops.start=unavailable_start;ops.poll=unavailable_poll;
 rc=runtime_root.empty()?demo_session_init(session.get(),&store,1,&ops):demo_release_install(session.get(),&store,runtime_root.c_str());service.configured=!runtime_root.empty()&&!rc;if(rc){fprintf(stderr,"recovery error %d\n",rc);return 1;}
 listener=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);if(listener<0)return 1;
 /* Do not unlink an unverified existing socket. A stale endpoint is explicit recovery. */
 if(bind(listener,reinterpret_cast<sockaddr *>(&addr),sizeof(addr))||chmod(socket_path.c_str(),0600)||listen(listener,8)){perror("local socket");return 1;}
 for(;;){
  int client=accept4(listener,nullptr,nullptr,SOCK_CLOEXEC);
  if(client<0){if(errno==EINTR)continue;return 1;}
  struct ucred peer{};socklen_t peer_bytes=sizeof(peer);
  struct timeval timeout{5,0};setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
  std::string message;
  if(getsockopt(client,SOL_SOCKET,SO_PEERCRED,&peer,&peer_bytes)||peer.uid!=geteuid())write_message(client,response(-EACCES,"PEER_REJECTED"));
  else if(!read_message(client,message))write_message(client,response(-EINVAL,"TRUNCATED_REQUEST"));
  else write_message(client,dispatch(session.get(),service,message));
  close(client);
 }
}
bool decode(const std::string &encoded,std::string &out){
 static const std::string alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
 if(encoded.empty()||encoded.size()>((DEMO_MAX_JSON+2)/3)*4||encoded.size()%4)return false;
 out.clear();
 unsigned value=0,bits=0;bool padding=false;unsigned pads=0;
 for(char c:encoded){
  if(c=='='){padding=true;if(++pads>2)return false;continue;}
  auto index=alphabet.find(c);if(padding||index==std::string::npos)return false;
  value=(value<<6)|static_cast<unsigned>(index);bits+=6;
  if(bits>=8){bits-=8;out.push_back(static_cast<char>((value>>bits)&255));}
 }
 if(out.size()>DEMO_MAX_JSON||(bits&&((value&((1u<<bits)-1))!=0)))return false;
 return pads==bits/2;
}
}
int main(int argc,char **argv){
 std::string socket_path="/run/vxl-demo/demo.sock",store="/var/lib/vxl-demo",runtime_root,action,encoded;
 bool server=false,stdin_request=false;
 for(int i=1;i<argc;i++){
  std::string arg=argv[i];
  if(arg=="--serve-local")server=true;
  else if(arg=="--request-stdin")stdin_request=true;
  else if((arg=="--socket"||arg=="--store"||arg=="--runtime-root"||arg=="--request-b64")&&i+1<argc){std::string value=argv[++i];if(arg=="--socket")socket_path=value;else if(arg=="--store")store=value;else if(arg=="--runtime-root")runtime_root=value;else encoded=value;}
  else if(action.empty()&&allowed(arg))action=arg;
  else return 2;
 }
 if(server){if(!action.empty()||!encoded.empty()||stdin_request)return 2;return serve(store,socket_path,runtime_root);}
 if(action.empty()||!runtime_root.empty())return 2;
 std::string request="{}";
 if(stdin_request){
  if(!encoded.empty())return 2;
  request.clear();char buffer[4096];
  for(;;){ssize_t n=read(STDIN_FILENO,buffer,sizeof(buffer));if(n<0&&errno==EINTR)continue;if(n<0)return 2;if(!n)break;request.append(buffer,static_cast<size_t>(n));if(request.size()>DEMO_MAX_JSON)return 2;}
 }
 if(!encoded.empty()&&!decode(encoded,request))return 2;
 json_tokener *tok=json_tokener_new_ex(32);json_tokener_set_flags(tok,JSON_TOKENER_STRICT|JSON_TOKENER_VALIDATE_UTF8);
 json_object *r=json_tokener_parse_ex(tok,request.data(),static_cast<int>(request.size()));
 bool valid=json_tokener_get_error(tok)==json_tokener_success&&json_tokener_get_parse_end(tok)==request.size()&&r&&json_object_is_type(r,json_type_object);json_tokener_free(tok);
 if(!valid){json_object_put(r);return 2;}
 json_object *o=json_object_new_object();json_object_object_add(o,"action",json_object_new_string(action.c_str()));json_object_object_add(o,"request",r);
 std::string message=json_object_to_json_string_ext(o,JSON_C_TO_STRING_PLAIN);json_object_put(o);
 sockaddr_un addr{};if(!address(socket_path,addr))return 2;
 int client=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);if(client<0)return 1;
 struct timeval timeout{5,0};setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
 if(connect(client,reinterpret_cast<sockaddr *>(&addr),sizeof(addr))||!write_message(client,message)){perror("local runner");close(client);return 1;}
 std::string reply;bool ok=read_message(client,reply);close(client);if(!ok)return 1;puts(reply.c_str());
 json_object *response_obj=json_tokener_parse(reply.c_str());json_object *success=response_obj?json_object_object_get(response_obj,"ok"):nullptr;
 int result=success&&json_object_get_boolean(success)?0:1;json_object_put(response_obj);return result;
}

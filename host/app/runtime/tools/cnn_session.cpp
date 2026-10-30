#include "cnn_session.h"
#include <vxl/cnn.h>
#include "../generated/cnn/programs.h"
#include <cerrno>
#include <functional>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <fcntl.h>
#include <unistd.h>
namespace {
const unsigned counts[9]={64,18,2,72,72,18,36,2,2};
const unsigned mapping[4][4]={{0,1,2,3},{3,99,99,4},{4,99,99,5},{5,6,7,8}};
const vxl_u32 *programs[]={vxl_cnn_program_2,vxl_cnn_program_3,vxl_cnn_program_4,vxl_cnn_program_5};
vxl_request request(){vxl_request r{};r.abi_major=1;r.struct_size=sizeof(r);return r;}
std::vector<uint32_t> guarded(const int32_t *p,unsigned n){
 unsigned padded=(n*4+63)/64*16;std::vector<uint32_t> blob(16+padded+16,VXL_CNN_PADDING);
 for(unsigned i=0;i<16;i++){blob[i]=VXL_CNN_LEFT;blob[16+padded+i]=VXL_CNN_RIGHT;}
 for(unsigned i=0;i<n;i++)blob[16+i]=p?static_cast<uint32_t>(p[i]):VXL_CNN_POISON;
 return blob;
}
int timed(CnnInterval &t,CnnNowFn now,void *ctx,const std::function<int()> &op){
 int rc=cnn_interval_begin(&t,now,ctx);if(rc)return rc;
 rc=op();int clock_rc=cnn_interval_end(&t,now,ctx);if(rc)t.result=rc;return rc?rc:clock_rc;
}
int add(uint64_t &a,uint64_t b){if(a>UINT64_MAX-b)return -EOVERFLOW;a+=b;return 0;}
}
CnnSession::CnnSession()=default;
CnnSession::CnnSession(CnnIo io):io_(io),injected_(true){}
int CnnSession::open(){
 if(closed_)return -EBADF;
 if(opened_)return 0;
 if(injected_){if(!io_.call||!io_.close)return -EINVAL;opened_=true;return 0;}
 int rc=vxl_transport_open(&transport_,0);if(!rc)opened_=true;return rc;
}
int CnnSession::call(uint32_t cmd,vxl_request *r){if(!opened_||closed_)return -EBADF;return injected_?io_.call(io_.ctx,cmd,r):vxl_transport_call(&transport_,cmd,r,sizeof(*r));}
int CnnSession::close(){
 if(closed_)return close_result_;
 closed_=true;
 if(opened_)close_result_=injected_?io_.close(io_.ctx):vxl_transport_close_checked(&transport_);
 opened_=false;return close_result_;
}
int CnnSession::copy(unsigned index,void *p,bool write,uint64_t *confirmed){
 if(confirmed)*confirmed=0;
 if(index>=objects.size())return -EINVAL;
 auto r=request();r.handle=objects[index].handle;r.length=objects[index].initial.size()*4;r.user_ptr=reinterpret_cast<uintptr_t>(p);
 int rc=call(write?VXL_COPY_TO:VXL_COPY_FROM,&r);
 if(r.used>r.length)return -EIO;
 if(confirmed)*confirmed=r.used;
 if(!rc&&r.used!=r.length)return -EIO;
 return rc;
}
int CnnSession::setup(const CnnWeights &w,const int32_t x[64],vxl_request *info){
 int rc=open();if(rc)return rc;*info=request();rc=call(VXL_GET_INFO,info);if(rc)return rc;
 if(info->state!=VXL_READY||info->profile_id||info->alignment!=64||info->capacity!=VXL_PL_SIZE||info->used)return -ENODEV;
 jobs_before=info->jobs;
 objects[0].initial.assign(1024,0);objects[1].initial.assign(16,0);
 const int32_t *inputs[]={x,w.conv,w.conv_bias,nullptr,nullptr,nullptr,w.fc,w.fc_bias,nullptr};
 for(unsigned i=0;i<9;i++){objects[i+2].count=counts[i];objects[i+2].initial=guarded(inputs[i],counts[i]);}
 for(unsigned i=0;i<11;i++){
  auto r=request();r.domain=i<2?VXL_PS:VXL_PL;r.purpose=i==0?VXL_CODE:i==1?VXL_ARGS:VXL_DATA;
  r.length=objects[i].initial.size()*4;r.alignment=64;r.address=i==0?VXL_CNN_CODE_GPU:i==1?VXL_CNN_ARGS_GPU:0;
  rc=call(i<2?VXL_RESERVE:VXL_ALLOC,&r);if(rc)return rc;
  objects[i].handle=r.handle;objects[i].gpu=r.gpu_address;
  if(!r.handle||(i<2&&r.gpu_address!=r.address)||(r.gpu_address&63))return -ERANGE;
 }
 for(unsigned i=2;i<11;i++){rc=copy(i,objects[i].initial.data(),true);if(rc)return rc;}
 return 0;
}
int CnnSession::cleanup(CnnRun *run){
 int rc=0;if(opened_&&!closed_){
  auto r=request();rc=call(VXL_GET_INFO,&r);
  if(rc||r.state==VXL_RUNNING||r.state==VXL_QUARANTINED){run->pinned_retained=true;if(!rc)rc=-EBUSY;}
  else {
   for(auto &o:objects)if(o.handle){auto f=request();f.handle=o.handle;int freed=call(VXL_FREE,&f);if(freed){rc=freed;break;}o.handle=0;}
   if(!rc){r=request();rc=call(VXL_GET_INFO,&r);run->final_info=r;run->final_info_valid=!rc;if(!rc&&(r.state!=VXL_READY||r.used))rc=-EIO;}
  }
 }
 run->cleanup_result=rc;run->close_result=close();return rc?rc:run->close_result;
}
CnnWeights cnn_fixed_weights(){
 CnnWeights w{};for(unsigned i=0;i<18;i++)w.conv[i]=static_cast<int32_t>((3*i+2)%7)-3;
 w.conv_bias[0]=-1;w.conv_bias[1]=2;for(unsigned i=0;i<36;i++)w.fc[i]=static_cast<int32_t>((5*i+3)%7)-3;w.fc_bias[0]=2;w.fc_bias[1]=-2;return w;
}
int cnn_run_iteration(CnnSession &s,const CnnWeights &weights,const int32_t input[64],const CnnOutputs &gold,CnnNowFn now,void *ctx,CnnIteration *out){
 int rc=cnn_interval_begin(&out->audit,now,ctx);if(rc){out->result=rc;return rc;}
 auto finish=[&](int result){int clock_rc=cnn_interval_end(&out->audit,now,ctx);out->result=result?result:clock_rc;return out->result;};
 auto transfer=[&](unsigned obj,std::vector<uint32_t> &blob,bool write){
  CnnTransfer t;t.object=obj;t.write=write;t.bytes=blob.size()*4;t.payload_bytes=s.objects[obj].count*4;
  t.result=timed(t.time,now,ctx,[&]{return s.copy(obj,blob.data(),write,&t.confirmed_bytes);});
  out->transfers.push_back(t);
  if(add(write?out->h2d_bytes:out->d2h_bytes,t.confirmed_bytes))return -EOVERFLOW;
  if(t.time.valid&&add(write?out->tensor_h2d_ns:out->tensor_d2h_ns,t.time.elapsed_ns))return -EOVERFLOW;
  if(t.result)return t.result;
  return 0;
 };
 s.objects[2].initial=guarded(input,64);s.objects[3].initial=guarded(weights.conv,18);
 s.objects[4].initial=guarded(weights.conv_bias,2);s.objects[8].initial=guarded(weights.fc,36);
 s.objects[9].initial=guarded(weights.fc_bias,2);
 for(unsigned i=2;i<11;i++){rc=transfer(i,s.objects[i].initial,true);if(rc)return finish(rc);}
 const int32_t *answers[]={gold.z,gold.r,gold.p,gold.y};const unsigned output_index[]={3,4,5,8};
 for(unsigned layer=0;layer<4;layer++){
  auto &l=out->layers[layer];l.program_id=layer+2;
  vxl_u32 args[16]={VXL_CNN_MAGIC,64,1,layer+2};
  for(unsigned j=0;j<4;j++){unsigned i=mapping[layer][j];if(i!=99){args[4+j]=s.objects[i+2].gpu+64;args[8+j]=counts[i];}}
  rc=timed(l.code_args,now,ctx,[&]{int code=s.copy(0,const_cast<vxl_u32*>(programs[layer]),true,&l.code_h2d_bytes);return code?code:s.copy(1,args,true,&l.args_h2d_bytes);});
  if(add(out->h2d_bytes,l.code_h2d_bytes+l.args_h2d_bytes)){l.result=-EOVERFLOW;return finish(l.result);}
  if(rc){l.result=rc;return finish(rc);}
  rc=timed(l.submit_wait,now,ctx,[&]{
   auto r=request();r.handle=s.objects[0].handle;r.handle2=s.objects[1].handle;r.trust_id=layer+2;r.deadline_ms=10000;
   int result=s.call(VXL_SUBMIT,&r);l.job_id=r.job_id;if(result)return result;
   if(!l.job_id)return -EIO;
   r=request();r.job_id=l.job_id;r.wait_ms=10000;result=s.call(VXL_WAIT,&r);
   if(!result&&(r.state!=VXL_COMPLETED||r.result))result=r.result?r.result:-EIO;
   return result;
  });
  auto status=request();int status_rc=s.call(VXL_GET_STATUS,&status);l.snapshot=status;l.snapshot_valid=!status_rc;
  if(rc){if(!l.job_id&&!status_rc)l.job_id=status.job_id;l.result=rc;return finish(rc);}
  if(status_rc||status.state!=VXL_COMPLETED||status.job_id!=l.job_id||status.result||!(status.hwstatus&2)||status.errors||status.pending_w||status.pending_r||status.jobs!=static_cast<uint32_t>(s.jobs_before+1)){
   l.result=status_rc?status_rc:-EIO;return finish(l.result);
  }
  s.jobs_before=status.jobs;if(add(out->kernel_chain_host_ns,l.submit_wait.elapsed_ns)){l.result=-EOVERFLOW;return finish(l.result);}
  auto reap=request();reap.job_id=l.job_id;rc=s.call(VXL_REAP,&reap);if(rc){l.result=rc;return finish(rc);}
  for(unsigned i=0;i<9;i++){
   l.raw[i].resize(s.objects[i+2].initial.size());rc=transfer(i+2,l.raw[i],false);
   l.d2h_bytes+=out->transfers.back().confirmed_bytes;
   if(rc){l.result=rc;return finish(rc);}l.raw_valid[i]=true;
  }
  rc=timed(l.verify,now,ctx,[&]{
   l.guard_pass=true;
   for(unsigned i=0;i<9;i++){
    auto expected=s.objects[i+2].initial;
    for(unsigned done=0;done<=layer;done++)if(i==output_index[done])for(unsigned j=0;j<counts[i];j++)expected[16+j]=static_cast<uint32_t>(answers[done][j]);
    for(unsigned j=0;j<expected.size();j++)if(l.raw[i][j]!=expected[j]){
     if(j<16||j>=16+counts[i])l.guard_pass=false;
     else {l.mismatch_count++;int64_t diff=static_cast<int64_t>(static_cast<int32_t>(l.raw[i][j]))-static_cast<int64_t>(static_cast<int32_t>(expected[j]));uint64_t absolute=diff<0?static_cast<uint64_t>(-diff):static_cast<uint64_t>(diff);if(absolute>l.max_abs)l.max_abs=absolute;}
    }
   }
   return l.guard_pass&&!l.mismatch_count?0:-EILSEQ;
  });
  l.result=rc;if(rc)return finish(rc);
 }
 return finish(0);
}
int cnn_run_suite(CnnSession &s,const CnnWeights &weights,CnnRun *run){
 run->scope=s.scope();int32_t input[64];for(unsigned i=0;i<64;i++)input[i]=static_cast<int32_t>((5*i+1)%9)-4;
 int rc=cnn_clock_resolution(&run->clock_resolution_ns);
 if(!rc)rc=timed(run->setup,run->now,run->clock_ctx,[&]{return s.setup(weights,input,&run->initial_info);});
 if(!rc)for(unsigned index=0;index<24;index++){
  CnnIteration r;r.phase=index==0?"FIRST":index<4?"WARMUP":"MEASURED";r.iteration=index==0?0:index<4?index-1:index-4;CnnOutputs golden{};
  rc=timed(r.cpu_reference,run->now,run->clock_ctx,[&]{return cnn_reference(input,weights,&golden);});
  if(!rc)rc=cnn_run_iteration(s,weights,input,golden,run->now,run->clock_ctx,&r);else r.result=rc;
  run->iterations.push_back(std::move(r));if(rc)break;
 }
 int clean=timed(run->teardown,run->now,run->clock_ctx,[&]{return s.cleanup(run);});
 /* A clock failure before teardown must still close and retain pinned memory. */
 if(run->close_result==-61){int forced=s.cleanup(run);if(!clean)clean=forced;}
 run->result=rc?rc:clean;return run->result;
}

namespace {
std::string quote(const std::string &v){
 std::ostringstream out;out<<'"';
 for(unsigned char c:v){
  if(c=='"'||c=='\\')out<<'\\'<<c;
  else if(c<32){const char *hex="0123456789abcdef";out<<"\\u00"<<hex[c>>4]<<hex[c&15];}
  else out<<c;
 }
 out<<'"';return out.str();
}
void interval(std::ostream &o,const CnnInterval &t){
 o<<"{\"start_ns\":"<<t.start_ns<<",\"end_ns\":"<<t.end_ns<<",\"elapsed_ns\":"<<(t.valid?std::to_string(t.elapsed_ns):"null")<<",\"valid\":"<<(t.valid?"true":"false")<<",\"result\":"<<t.result<<'}';
}
void snapshot(std::ostream &o,const vxl_request &r){
 o<<"{\"state\":"<<r.state<<",\"generation\":"<<r.generation<<",\"profile_id\":"<<r.profile_id<<",\"job_id\":"<<r.job_id<<",\"jobs\":"<<r.jobs<<",\"hwstatus\":"<<r.hwstatus<<",\"errors\":"<<r.errors<<",\"pending_w\":"<<r.pending_w<<",\"pending_r\":"<<r.pending_r<<",\"result\":"<<r.result<<",\"used\":"<<r.used<<'}';
}
int write_file(const std::string &path,const void *data,size_t n){
 int fd=::open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);if(fd<0)return -errno;
 const char *p=static_cast<const char*>(data);int rc=0;
 while(n){
  ssize_t wrote=::write(fd,p,n);
  if(wrote<0){if(errno==EINTR)continue;rc=-errno;break;}
  if(!wrote){rc=-EIO;break;}p+=wrote;n-=static_cast<size_t>(wrote);
 }
 if(!rc&&fsync(fd))rc=-errno;
 if(::close(fd)&&!rc)rc=-errno;
 return rc;
}
int write_text(const std::string &path,const std::string &text){return write_file(path,text.data(),text.size());}
}
std::string cnn_iteration_json(const CnnIteration &r,const char *scope){
 std::ostringstream o;o<<"{\"scope\":"<<quote(scope)<<",\"phase\":"<<quote(r.phase)<<",\"iteration\":"<<r.iteration<<",\"result\":"<<r.result;
 o<<",\"start_ns\":"<<r.audit.start_ns<<",\"end_ns\":"<<r.audit.end_ns<<",\"cnn_audit_end_to_end_ns\":"<<(r.audit.valid?std::to_string(r.audit.elapsed_ns):"null");
 o<<",\"kernel_chain_host_ns\":"<<r.kernel_chain_host_ns<<",\"cpu_reference_ns\":"<<(r.cpu_reference.valid?std::to_string(r.cpu_reference.elapsed_ns):"null");
 o<<",\"tensor_h2d_ns\":"<<r.tensor_h2d_ns<<",\"tensor_d2h_ns\":"<<r.tensor_d2h_ns<<",\"bytes\":{\"h2d\":"<<r.h2d_bytes<<",\"d2h\":"<<r.d2h_bytes<<"},\"device_kernel_ns\":null,\"cycles\":null,\"frequency\":null,\"layers\":[";
 for(unsigned i=0;i<4;i++){
  if(i)o<<',';
  const auto &l=r.layers[i];
  const char *hashes[]={VXL_CNN_SHA_2,VXL_CNN_SHA_3,VXL_CNN_SHA_4,VXL_CNN_SHA_5};
  o<<"{\"program_id\":"<<l.program_id<<",\"program_sha256\":"<<quote(hashes[i])<<",\"job_id\":"<<l.job_id<<",\"result\":"<<l.result<<",\"snapshot_valid\":"<<(l.snapshot_valid?"true":"false")<<",\"snapshot\":";snapshot(o,l.snapshot);
  o<<",\"guard_pass\":"<<(l.guard_pass?"true":"false")<<",\"mismatch_count\":"<<l.mismatch_count<<",\"max_abs\":"<<l.max_abs;
  o<<",\"start_ns\":"<<l.submit_wait.start_ns<<",\"end_ns\":"<<l.submit_wait.end_ns<<",\"layer_submit_wait_ns\":"<<(l.submit_wait.valid?std::to_string(l.submit_wait.elapsed_ns):"null");
  o<<",\"layer_code_args_h2d_ns\":"<<(l.code_args.valid?std::to_string(l.code_args.elapsed_ns):"null")<<",\"layer_verify_ns\":"<<(l.verify.valid?std::to_string(l.verify.elapsed_ns):"null")<<",\"bytes\":{\"h2d\":"<<l.code_h2d_bytes+l.args_h2d_bytes<<",\"d2h\":"<<l.d2h_bytes<<"},\"code_args_time\":";interval(o,l.code_args);o<<",\"verify_time\":";interval(o,l.verify);
  o<<",\"raw_valid\":[";for(unsigned j=0;j<9;j++){if(j)o<<',';o<<(l.raw_valid[j]?"true":"false");}o<<"]}";
 }
 o<<"],\"transfers\":[";
 for(size_t i=0;i<r.transfers.size();i++){
  if(i)o<<',';
  const auto &t=r.transfers[i];o<<"{\"object\":"<<t.object<<",\"direction\":"<<quote(t.write?"H2D":"D2H")<<",\"bytes\":"<<t.bytes<<",\"confirmed_bytes\":"<<t.confirmed_bytes<<",\"payload_bytes\":"<<t.payload_bytes<<",\"guard_bytes\":128,\"padding_bytes\":"<<t.bytes-t.payload_bytes-128<<",\"result\":"<<t.result<<",\"time\":";interval(o,t.time);o<<'}';
 }
 o<<"]}";return o.str();
}
int cnn_save_run(const std::string &dir,CnnRun *run){
 if(!run)return -EINVAL;
 auto fail=[&](int rc){run->save_result=rc;if(!run->result)run->result=rc;return rc;};
 std::error_code error;if(!std::filesystem::is_directory(dir,error))return fail(-ENOENT);
 std::filesystem::create_directory(dir+"/raw",error);if(error)return fail(-error.value());
 std::string jsonl;
 for(size_t i=0;i<run->iterations.size();i++){
  const auto &r=run->iterations[i];jsonl+=cnn_iteration_json(r,run->scope.c_str())+'\n';
  for(unsigned l=0;l<4;l++)for(unsigned j=0;j<9;j++)if(!r.layers[l].raw[j].empty()){
   std::string path=dir+"/raw/iteration-"+std::to_string(i)+"-layer-"+std::to_string(l)+"-object-"+std::to_string(j)+".bin";
   const auto &raw=r.layers[l].raw[j];int rc=write_file(path,raw.data(),raw.size()*4);if(rc)return fail(rc);
  }
 }
 int rc=write_text(dir+"/timing.jsonl",jsonl);if(rc)return fail(rc);
 if(!run->environment.empty()){rc=write_text(dir+"/environment.txt",run->environment);if(rc)return fail(rc);}
 if(!run->manifest_json.empty()){rc=write_text(dir+"/release-manifest.json",run->manifest_json);if(rc)return fail(rc);}
 run->save_result=0;
 std::ostringstream o;o<<"{\"scope\":"<<quote(run->scope)<<",\"build_id\":"<<quote(run->build_id)<<",\"boot_id\":"<<quote(run->boot_id)<<",\"kernel\":"<<quote(run->kernel)<<",\"source_commit\":"<<quote(run->source_commit)<<",\"clock\":\"CLOCK_MONOTONIC\",\"clock_resolution_ns\":"<<run->clock_resolution_ns<<",\"result\":"<<run->result<<",\"cleanup_result\":"<<run->cleanup_result<<",\"close_result\":"<<run->close_result<<",\"save_result\":0,\"pinned_retained\":"<<(run->pinned_retained?"true":"false")<<",\"iterations\":"<<run->iterations.size()<<",\"setup\":";interval(o,run->setup);o<<",\"teardown\":";interval(o,run->teardown);
 o<<",\"initial_info\":";snapshot(o,run->initial_info);o<<",\"final_info_valid\":"<<(run->final_info_valid?"true":"false")<<",\"final_info\":";snapshot(o,run->final_info);o<<"}\n";
 std::string text=o.str();text.pop_back();text.pop_back();
 text+=",\"utc_start\":"+quote(run->utc_start)+"}\n";
 rc=write_text(dir+"/run.json",text);if(rc)return fail(rc);return 0;
}

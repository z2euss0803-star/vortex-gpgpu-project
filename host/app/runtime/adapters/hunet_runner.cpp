#include <vxl/demo_hunet_runner.h>
#include <vxl/demo_frame.h>
#include <vxl/hunet.h>
#include "../tools/hunet_fused_session.h"
#include "../tools/hunet_opt_timing.h"
#include <json-c/json.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
namespace {
struct State { FusedRaw raw;vxl_opt_load_timing load{};demo_result result{};uint64_t cycles=0; };
struct JsonDelete {void operator()(json_object *o)const{json_object_put(o);}};
using Json=std::unique_ptr<json_object,JsonDelete>;
void need(int rc){if(rc)throw rc;}
void require(bool yes,int error=-EINVAL){if(!yes)throw error;}
std::string hex(const uint8_t *p){char h[65];static const char d[]="0123456789abcdef";for(unsigned i=0;i<32;++i){h[i*2]=d[p[i]>>4];h[i*2+1]=d[p[i]&15];}h[64]=0;return h;}
json_object *get(json_object *o,const char *name){json_object *v=nullptr;if(o)json_object_object_get_ex(o,name,&v);return v;}
void text(json_object *o,const char *name,const char *value){json_object_object_add(o,name,json_object_new_string(value));}
void num(json_object *o,const char *name,uint64_t value){json_object_object_add(o,name,json_object_new_uint64(value));}
void flag(json_object *o,const char *name,bool value){json_object_object_add(o,name,json_object_new_boolean(value));}
bool nonzero(const uint8_t *p,size_t n){uint8_t a=0;for(size_t i=0;i<n;++i)a|=p[i];return a!=0;}
void save(demo_hunet_adapter &a,const demo_run &run,const std::string &name,const void *p,size_t n){if(n)need(demo_store_run_asset(a.config->store,run.run_id,name.c_str(),p,n));}
void asset(json_object *array,const char *name,const char *dtype,const char *role,const void *p,size_t n,std::initializer_list<uint32_t> dims){
 char sha[65];demo_sha_hex(p,n,sha);auto *o=json_object_new_object(),*shape=json_object_new_array();text(o,"name",name);text(o,"dtype",dtype);text(o,"role",role);text(o,"sha256",sha);num(o,"bytes",n);for(auto d:dims)json_object_array_add(shape,json_object_new_uint64(d));json_object_object_add(o,"shape",shape);json_object_array_add(array,o);
}
int fail(demo_hunet_adapter &a,const demo_run &run,int rc){
 if(!a.first_error){a.first_error=rc;}
 char message[256];int n=snprintf(message,sizeof(message),"{\"state\":\"FAIL_STOP\",\"library_errno\":%d,\"natural_exit\":null,\"gpu_raw_cycles\":null,\"automatic_retry\":false}\n",a.first_error);
 if(a.config&&a.config->store)(void)demo_store_run_asset(a.config->store,run.run_id,"error-hunet-runner.json",message,size_t(n));
 return a.first_error;
}
void identify(demo_hunet_adapter &a,const demo_run &r,json_object *o){
 const auto &c=*a.config;const auto &b=a.client.binding;require(r.app==DEMO_HUNET&&r.generation==b.v2.v1.generation,-ESTALE);
 const char *scope=json_object_get_string(get(o,"scope"));require(scope!=nullptr);
#ifdef DEMO_SOFTWARE_MODEL_TEST
 require(!strcmp(scope,"OFFLINE"),-EPERM);
#else
 require(!strcmp(scope,"REAL_BOARD"),-EPERM);
#endif
 auto *shape=get(o,"shape");require(shape&&json_object_array_length(shape)==4);unsigned expected[]={1,3,128,128};for(unsigned i=0;i<4;++i)require(json_object_get_uint64(json_object_array_get_idx(shape,i))==expected[i]);
 uint8_t sha[32];vxl_hunet_hash(c.input,c.input_bytes,sha);auto *ids=get(o,"identities");const char *keys[]={"hardware","source","program","model","input","quant","layout"};const uint8_t *values[]={c.hardware_sha,b.v2.v1.source_sha256,b.v2.v1.program_sha256,b.v2.v1.model_sha256,sha,b.v2.v1.weights_sha256,b.layout_sha256};
 for(unsigned i=0;i<7;++i){const char *s=json_object_get_string(get(ids,keys[i]));require(s&&hex(values[i])==s,-ESTALE);}
 char frame_sha[65];demo_sha_hex(c.source_frame->pixels,c.source_frame->bytes,frame_sha);require(!strcmp(frame_sha,c.source_frame->sha256)&&!strcmp(frame_sha,c.source_frame->raw_source_sha),-ESTALE);
 require(get(o,"roi")&&json_object_get_uint64(get(o,"roi"))==c.roi,-ESTALE);
 const char *names[]={"classes_sha256","glyph_sha256","display_input_sha256"};const char *wants[]={c.labels->classes_sha,c.labels->glyph_sha,c.source_frame->sha256};for(unsigned i=0;i<3;++i){const char *s=json_object_get_string(get(o,names[i]));require(s&&!strcmp(s,wants[i]),-ESTALE);}
}
/* New executable identities start with flags0. Only explicit diagnostics may
 * collect first-run evidence; they never mint or inherit measured approval. */
void gate(demo_hunet_adapter &a,FusedRaw &r,bool diagnostics){
 const auto &c=*a.config;uint32_t flags,roi;memcpy(&flags,c.gate+8,4);memcpy(&roi,c.gate+12,4);
 auto binding=a.client.binding;binding.v2.v1.generation=binding.v2.v1.state=0;uint8_t sha[32];vxl_hunet_hash(&binding,512,sha);
 require(!memcmp(c.gate,"VXFUSG1",8)&&roi==c.roi&&!memcmp(c.gate+16,sha,32)&&!nonzero(c.gate+96,32),-EPERM);
 bool approved=flags==7&&nonzero(c.gate+48,32)&&nonzero(c.gate+80,16);
 bool pending=flags==0&&!nonzero(c.gate+48,32)&&!nonzero(c.gate+80,16);
 require(approved||(diagnostics&&pending),-EPERM);
 if(!diagnostics){r.measured_authorized=true;memcpy(r.numeric_gate_binding.data(),sha,32);}
}
#ifndef DEMO_SOFTWARE_MODEL_TEST
void libm_identity(const demo_hunet_config &c){Dl_info info{};require(dladdr(reinterpret_cast<void*>(erff),&info)&&info.dli_fname,-ESTALE);char *resolved=realpath(info.dli_fname,nullptr);require(resolved!=nullptr,-ENOENT);std::string path(resolved);free(resolved);std::ifstream f(path,std::ios::binary);require(bool(f),-ENOENT);vxl_hunet_sha hash;vxl_hunet_sha_init(&hash);char data[4096];size_t total=0;while(f){f.read(data,sizeof(data));size_t n=size_t(f.gcount());total+=n;require(total<=67108864,-EFBIG);vxl_hunet_sha_update(&hash,data,n);}require(!f.bad()&&total>0,-EIO);uint8_t sha[32];vxl_hunet_sha_final(&hash,sha);require(!memcmp(sha,c.libm_sha,32),-ESTALE);}
#endif
int open_owner(void *){int fd=open("/dev/vortex0",O_RDWR|O_NOFOLLOW|O_CLOEXEC);return fd<0?-errno:fd;}
int quiet(void *p,int fd){auto &a=*static_cast<demo_hunet_adapter*>(p);if(a.first_error||a.client.gpu.stopped)return 0;vxl_request r{};r.abi_major=1;r.struct_size=176;if(ioctl(fd,VXL_GET_INFO,&r)<0)return -errno;return r.profile_id==4&&r.state==VXL_READY&&!r.errors&&!r.pending_r&&!r.pending_w?1:0;}
int release(void *p,int fd){int rc=demo_hunet_adapter_detach(p,fd);return rc?rc:close(fd)<0?-errno:0;}
int start(void *p,int fd,const demo_run *run,uint64_t *id){
 auto &a=*static_cast<demo_hunet_adapter*>(p);if(!run||!id||!a.state||a.first_error)return a.first_error?a.first_error:-EINVAL;auto &s=*static_cast<State*>(a.state);const auto &cfg=*a.config;a.completed=0;s.raw=FusedRaw{};s.cycles=0;Json o(json_tokener_parse(run->json));
 try{
  require(bool(o));if(a.client.gpu.fd<0)need(vxl_fused_client_attach_fd(&a.client,fd));require(a.client.gpu.fd==fd,-EBADF);identify(a,*run,o.get());gate(a,s.raw,run->diagnostics);
#ifndef DEMO_SOFTWARE_MODEL_TEST
  libm_identity(cfg);
#endif
  save(a,*run,"frame-hunet-binding.bin",&a.client.binding,512);save(a,*run,"frame-hunet-gate.bin",cfg.gate,128);save(a,*run,"frame-hunet-roi.f32",cfg.input,cfg.input_bytes);save(a,*run,"frame-hunet-source.rgb",cfg.source_frame->pixels,cfg.source_frame->bytes);
  bool cold=!a.client.gpu.loaded;if(cold)need(vxl_fused_client_load_timed(&a.client,cfg.release_root,&s.load));else memset(&s.load,0,sizeof(s.load));
  s.raw.load_ns=cold?s.load.end_ns-s.load.start_ns:0;
  s.raw.checkpoint=[&](const std::string &name,const void *data,size_t n){std::string safe="frame-hunet-"+name;for(char &c:safe)if(c=='/'||c=='_')c='-';save(a,*run,safe,data,n);};
  unsigned observed=0;s.raw.gpu_checked=[&](const FusedGpuRecord &record){char name[80];snprintf(name,sizeof(name),"frame-hunet-gpu-%03u.bin",++observed);save(a,*run,name,&record,sizeof(record));
#ifndef DEMO_SOFTWARE_MODEL_TEST
   const auto &e=record.evidence;require(e.submit_ns&&e.done_ns>=e.submit_ns&&e.ack_ns>=e.done_ns&&(e.valid_flags&VXL_HUNET_VALID_TIMING)&&e.elapsed_raw&&e.elapsed_raw<e.watchdog,-ENODATA);require(s.cycles<=UINT64_MAX-e.elapsed_raw,-EOVERFLOW);s.cycles+=e.elapsed_raw;
#endif
  };
  vxl_hunet_begin_v1 b{};b.magic=VXL_HUNET_BEGIN_MAGIC;b.version=1;b.bytes=128;b.roi_index=cfg.roi;b.sample_kind=VXL_HUNET_FIRST;b.input_ptr=uintptr_t(cfg.input);b.input_bytes=cfg.input_bytes;vxl_hunet_hash(cfg.input,cfg.input_bytes,b.input_sha256);for(unsigned i=0;i<16;++i){char two[3]={run->run_id[i*2],run->run_id[i*2+1],0};b.run_id[i]=uint8_t(strtoul(two,nullptr,16));}
  need(hunet_fused_execute(&a.client,b,run->diagnostics?FusedMode::DIAGNOSTIC:FusedMode::MEASURED,&s.raw));require(observed==103&&s.raw.gpu.size()==103&&s.raw.cpu.size()==13&&s.raw.stage_count==100&&s.raw.captured_stages==(run->diagnostics?100u:0u)&&s.raw.logical_completed==1023,-EIO);
  demo_hunet_result decoded{};need(demo_hunet_decode(reinterpret_cast<const float*>(s.raw.logits.data()),s.raw.logits.size()/4,cfg.labels,&decoded));
  if(cfg.historical){uint8_t ref_sha[32];vxl_hunet_hash(cfg.historical,cfg.historical_count*4,ref_sha);require(!memcmp(ref_sha,cfg.historical_logits_sha,32),-ESTALE);require(cfg.historical_probe_json&&cfg.historical_probe_bytes&&cfg.historical_probe_bytes<=DEMO_MAX_JSON,-ENODATA);vxl_hunet_hash(cfg.historical_probe_json,cfg.historical_probe_bytes,ref_sha);require(!memcmp(ref_sha,cfg.historical_probe_sha,32),-ESTALE);Json probe(json_tokener_parse(cfg.historical_probe_json));require(bool(probe)&&get(probe.get(),"R2")&&get(probe.get(),"ORT"));const char *r2_status=json_object_get_string(get(get(probe.get(),"R2"),"status")),*ort_status=json_object_get_string(get(get(probe.get(),"ORT"),"status"));require(r2_status&&ort_status&&(!strcmp(r2_status,"PASS"))==bool(cfg.historical_r2_pass)&&(!strcmp(ort_status,"PASS"))==bool(cfg.historical_ort_pass),-ESTALE);save(a,*run,"frame-hunet-historical-probe.json",cfg.historical_probe_json,cfg.historical_probe_bytes);json_object_object_add(o.get(),"historical_R2",json_object_get(get(probe.get(),"R2")));json_object_object_add(o.get(),"historical_ORT",json_object_get(get(probe.get(),"ORT")));text(o.get(),"historical_probe_sha256",hex(cfg.historical_probe_sha).c_str());require(cfg.historical_count==DEMO_HUNET_CLASSES&&!memcmp(cfg.historical_input_sha,b.input_sha256,32)&&!memcmp(cfg.historical_model_sha,a.client.binding.v2.v1.model_sha256,32),-ESTALE);need(demo_hunet_classify(reinterpret_cast<const float*>(s.raw.logits.data()),cfg.historical,cfg.historical_count,cfg.historical_ort_pass,cfg.historical_r2_pass,&decoded));require(decoded.history_byte_equal,-EDOM);}
  strcpy(decoded.run_id,run->run_id);strcpy(decoded.source_result_sha,decoded.logits_sha);decoded.source_frame=cfg.source_frame;
#ifdef DEMO_SOFTWARE_MODEL_TEST
  decoded.view_scope=DEMO_VIEW_OFFLINE;
#else
  decoded.view_scope=DEMO_VIEW_LIVE;
#endif
  demo_frame frame{};need(demo_frame_from_hunet(&decoded,&frame));int store_rc=demo_store_run_asset(cfg.store,run->run_id,"frame-hunet-result.rgb",frame.pixels,frame.bytes);if(store_rc){demo_frame_free(&frame);throw store_rc;}
  auto *art=json_object_new_array();asset(art,"frame-hunet-roi.f32","FP32","input",cfg.input,cfg.input_bytes,{1,3,128,128});asset(art,"frame-hunet-source.rgb","RGB8","input",cfg.source_frame->pixels,cfg.source_frame->bytes,{cfg.source_frame->height,cfg.source_frame->width});asset(art,"frame-hunet-logits.f32","FP32","output",s.raw.logits.data(),s.raw.logits.size(),{8105});asset(art,"frame-hunet-result.rgb","RGB8","display",frame.pixels,frame.bytes,{frame.height,frame.width});demo_frame_free(&frame);json_object_object_add(o.get(),"artifacts",art);
  text(o.get(),"numeric_gate_action","UNCHANGED_REQUIRES_EXTERNAL_AUDIT");num(o.get(),"captured_stages",s.raw.captured_stages);text(o.get(),"state","RESULT_VALIDATED");text(o.get(),"numeric_class",decoded.numeric_class==DEMO_NUMERIC_KNOWN_DEVIATION?"KNOWN_NUMERIC_DEVIATION":decoded.numeric_class==DEMO_NUMERIC_PASS?"PASS":"NOT_EVALUATED");flag(o.get(),"history_byte_equal",decoded.history_byte_equal);flag(o.get(),"ort_evaluated",decoded.ort_evaluated);flag(o.get(),"ort_pass",decoded.ort_pass);flag(o.get(),"r2_evaluated",decoded.r2_evaluated);flag(o.get(),"r2_pass",decoded.r2_pass);text(o.get(),"precision","FP32_GPU_AND_CPU_ERF4_NOT_MOSAIC_INT16");
#ifdef DEMO_SOFTWARE_MODEL_TEST
  flag(o.get(),"cycle_valid",false);flag(o.get(),"timing_valid",false);json_object_object_add(o.get(),"raw_cycles",nullptr);text(o.get(),"timing_scope","HOST_PROTOCOL_MODEL_NOT_GPU_OR_BOARD_TIMING");
#else
  flag(o.get(),"cycle_valid",true);flag(o.get(),"timing_valid",true);num(o.get(),"raw_cycles",s.cycles);text(o.get(),"timing_scope","LINUX_MONOTONIC_AND_SEPARATE_GPU_RAW_CYCLES");
#endif
  auto *top=json_object_new_array();for(const auto &t:decoded.top){auto *entry=json_object_new_object();num(entry,"class_id",t.class_id);json_object_object_add(entry,"score",json_object_new_double(t.score));text(entry,"utf8",t.utf8);json_object_array_add(top,entry);}json_object_object_add(o.get(),"topk",top);
  auto *times=json_object_new_object();num(times,"diagnostic_capture",s.raw.diagnostic_capture_ns);num(times,"load",s.raw.load_ns);num(times,"inference_wall",s.raw.resident_ns);num(times,"begin",s.raw.roi_begin_call_ns);num(times,"end",s.raw.roi_end_call_ns);num(times,"final_read",s.raw.final_read_ns);num(times,"checkpoint_nested",s.raw.checkpoint_ns);uint64_t submit=0,evidence=0,reap=0,cpu=0,begin=0,commit=0,workers=0,sha=0;for(const auto &g:s.raw.gpu){submit+=g.submit_wait_ns;evidence+=g.evidence_ns;reap+=g.reap_ns;}for(const auto &c:s.raw.cpu){begin+=c.begin_call_ns;commit+=c.commit_call_ns;workers+=c.timing.duration_ns;}for(const auto &c:s.raw.cpu_detail){cpu+=c.handler_ns;sha+=c.input_sha_ns+c.output_sha_ns;}num(times,"gpu_submit_wait",submit);num(times,"gpu_evidence",evidence);num(times,"gpu_reap",reap);num(times,"cpu_handler",cpu);num(times,"cpu_begin",begin);num(times,"cpu_commit",commit);num(times,"cpu_workers_nested",workers);num(times,"cpu_sha_nested",sha);json_object_object_add(o.get(),"timings_ns",times);
  const char *json=json_object_to_json_string_ext(o.get(),JSON_C_TO_STRING_PLAIN);need(demo_parse_run(json,strlen(json),&s.result.run));demo_sha_hex(s.result.run.json,s.result.run.json_bytes,s.result.source_result_sha);require(a.serial<UINT64_MAX,-EOVERFLOW);*id=++a.serial;a.completed=1;return 0;
 }catch(int rc){return fail(a,*run,rc);}catch(...){return fail(a,*run,-EIO);}
}
int poll(void *p,int fd,uint64_t id){auto &a=*static_cast<demo_hunet_adapter*>(p);return a.first_error?a.first_error:fd!=a.client.gpu.fd||id!=a.serial?-ESTALE:a.completed?1:0;}
int collect(void *p,int fd,uint64_t id,demo_result *out){auto &a=*static_cast<demo_hunet_adapter*>(p);if(!out||poll(p,fd,id)!=1)return -ESTALE;*out=static_cast<State*>(a.state)->result;return 0;}
}
int demo_hunet_adapter_init(demo_hunet_adapter *a,const demo_hunet_config *c){
 if(!a)return -EINVAL;
 if(a->state)return -EBUSY;
 if(!c||!c->store||!c->release_root||!c->input||c->input_bytes!=196608||c->roi>26||!c->labels||c->labels->count!=8105||!c->source_frame||demo_frame_check(c->source_frame,0)||c->labels->bbox.roi_index!=c->roi||c->labels->bbox.image_width!=c->source_frame->width||c->labels->bbox.image_height!=c->source_frame->height||!c->gate||c->gate_bytes!=128)return -EINVAL;
#ifndef DEMO_SOFTWARE_MODEL_TEST
 if(!c->historical||c->historical_count!=8105||!c->historical_probe_json||!c->historical_probe_bytes||!nonzero(c->historical_probe_sha,32)||!nonzero(c->historical_logits_sha,32)||!nonzero(c->libm_sha,32))return -ENODATA;
#endif
 *a={};a->client.gpu.fd=-1;a->config=c;try{a->state=new State;}catch(...){return -ENOMEM;}return 0;
}
int demo_hunet_adapter_destroy(demo_hunet_adapter *a){if(!a)return -EINVAL;if(a->client.gpu.fd>=0||a->first_error||a->client.gpu.stopped)return -EBUSY;delete static_cast<State*>(a->state);a->state=nullptr;return 0;}
int demo_hunet_adapter_detach(void *p,int fd){auto &a=*static_cast<demo_hunet_adapter*>(p);if(a.first_error||a.client.gpu.stopped)return -EIO;if(a.client.gpu.fd<0)return 0;if(fd!=a.client.gpu.fd)return -EBADF;return vxl_fused_client_close(&a.client);}
int demo_hunet_adapter_ops(demo_hunet_adapter *a,demo_gpu_ops *ops){if(!a||!ops||!a->state)return -EINVAL;*ops={open_owner,release,start,poll,quiet,collect,a};return 0;}
int demo_hunet_run(demo_session *s,const demo_run *r,demo_result *out){if(!s||!r||!out||r->app!=DEMO_HUNET)return -EINVAL;if(s->gpu.start!=start&&(!s->dispatcher||s->dispatcher->active!=DEMO_HUNET||s->dispatcher->apps[1].start!=start))return -EINVAL;int rc=demo_start(s,r);return rc?rc:demo_status(s,r->run_id,out);}

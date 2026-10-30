#include <vxl/demo_mosaic.h>
#include <vxl/demo_frame.h>
#include <vxl/demo_dispatch.h>
#include <vxl/demo_timing.h>
#include <json-c/json.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
static void hex(const vxl_u8 *p,char out[65]){static const char s[]="0123456789abcdef";for(unsigned i=0;i<32;++i){out[i*2]=s[p[i]>>4];out[i*2+1]=s[p[i]&15];}out[64]=0;}
static int owner(void *){int fd=open("/dev/vortex0",O_RDWR|O_CLOEXEC);return fd<0?-errno:fd;}
static int quiet(void *context,int fd){auto &a=*static_cast<demo_mosaic_adapter*>(context);if(a.first_error||a.client.first_error||a.client.frame_active)return 0;vxl_request r{};r.abi_major=1;r.struct_size=176;r.profile_id=6;r.generation=a.client.generation;if(ioctl(fd,VXL_GET_INFO,&r)<0)return -errno;return r.profile_id==6&&r.state==VXL_READY&&!r.errors&&!r.pending_w&&!r.pending_r?1:0;}
static int release(void *context,int fd){auto &a=*static_cast<demo_mosaic_adapter*>(context);if(a.first_error)return a.first_error;int rc=a.client.attached?mosaic_client_close(&a.client):0;if(rc)return rc;return close(fd)<0?-errno:0;}
static json_object *get(json_object *o,const char *key){json_object *v=nullptr;json_object_object_get_ex(o,key,&v);return v;}
static int identify(demo_mosaic_adapter &a,const demo_run &run,json_object *o){
 if(run.app!=DEMO_MOSAIC||run.diagnostics||run.generation!=a.client.generation)return -EINVAL;
 const char *scope=json_object_get_string(get(o,"scope"));if(!scope)return -EINVAL;
#ifndef DEMO_SOFTWARE_MODEL_TEST
 if(strcmp(scope,"REAL_BOARD"))return -EPERM;
#else
 if(strcmp(scope,"OFFLINE"))return -EPERM;
#endif
 const auto &l=a.program->layouts[a.layout];json_object *shape=get(o,"shape");if(!shape||json_object_array_length(shape)!=4||json_object_get_int(json_object_array_get_idx(shape,0))!=1||json_object_get_int(json_object_array_get_idx(shape,1))!=3||json_object_get_uint64(json_object_array_get_idx(shape,2))!=l.layout.height||json_object_get_uint64(json_object_array_get_idx(shape,3))!=l.layout.width)return -EINVAL;
 const char *names[7]={"hardware","source","program","model","input","quant","layout"};char input_sha[65];demo_sha_hex(a.input,a.frame_bytes,input_sha);
 const vxl_u8 *digests[7]={a.client.binding.digests[MOSAIC_HARDWARE_SHA],a.client.binding.digests[MOSAIC_SOURCE_SHA],a.client.binding.digests[MOSAIC_FLAT_SHA],a.client.binding.digests[MOSAIC_MODEL_SHA],nullptr,a.program->assets->package_sha,l.layout_sha};
 auto *identities=get(o,"identities");for(unsigned i=0;i<7;++i){char expected[65];if(i==4)strcpy(expected,input_sha);else hex(digests[i],expected);const char *s=json_object_get_string(get(identities,names[i]));if(!s||strcmp(s,expected))return -ESTALE;}
 return 0;
}
static void artifact(json_object *array,const char *name,const char *type,const char *role,const void *bytes,size_t n,const uint32_t *shape,unsigned rank){char hash[65];demo_sha_hex(bytes,n,hash);auto *o=json_object_new_object(),*s=json_object_new_array();json_object_object_add(o,"name",json_object_new_string(name));json_object_object_add(o,"dtype",json_object_new_string(type));json_object_object_add(o,"role",json_object_new_string(role));json_object_object_add(o,"sha256",json_object_new_string(hash));json_object_object_add(o,"bytes",json_object_new_uint64(n));for(unsigned i=0;i<rank;++i)json_object_array_add(s,json_object_new_uint64(shape[i]));json_object_object_add(o,"shape",s);json_object_array_add(array,o);}
static int first_error(demo_mosaic_adapter &a,const demo_run &run,int rc){
 if(!a.first_error)a.first_error=rc;
 if(a.store){char message[256];int n=snprintf(message,sizeof(message),"{\"state\":\"FAIL_STOP\",\"library_errno\":%d,\"natural_exit\":null,\"automatic_retry\":false}\n",a.first_error);(void)demo_store_run_asset(a.store,run.run_id,"error-mosaic-runner.json",message,size_t(n));}
 return a.first_error;
}
static int start(void *context,int fd,const demo_run *run,uint64_t *job){
 auto &a=*static_cast<demo_mosaic_adapter*>(context);if(!run||!job||!a.store||!a.program||!a.assets||!a.input||!a.output||a.first_error)return -EINVAL;
 a.completed=0;json_object *o=json_tokener_parse(run->json);if(!o)return -EINVAL;int rc=0;
 if(!a.client.attached)rc=mosaic_client_attach_fd(&a.client,fd);
 else if(a.client.fd!=fd)rc=-EBADF;
 if(!rc)rc=identify(a,*run,o);
 bool cold=!a.client.loaded;
 if(!rc&&cold){rc=mosaic_client_configure(&a.client,a.program,a.assets,a.assets_bytes,a.layout);if(!rc)rc=mosaic_client_load(&a.client,a.program->assets);}
 int directory=-1,journal=-1;
 if(!rc){directory=openat(a.store->dir_fd,run->run_id,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(directory<0)rc=-errno;}
 if(!rc){journal=openat(directory,"mosaic-raw-evidence.bin",O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(journal<0)rc=-errno;}
 if(!rc)rc=mosaic_client_frame(&a.client,a.input,a.frame_bytes,a.output,a.frame_bytes,journal);
 if(!rc)rc=mosaic_client_run(&a.client,run,&a.result);
 if(journal>=0){if(fsync(journal)&&!rc)rc=-errno;if(close(journal)&&!rc)rc=-errno;}
 if(directory>=0){if(fsync(directory)&&!rc)rc=-errno;close(directory);}
 if(rc){json_object_put(o);return first_error(a,*run,rc);}
 const auto &layout=a.program->layouts[a.layout].layout;demo_frame frame{},source{};rc=demo_frame_from_mosaic(static_cast<const float*>(a.output),layout.width,layout.height,&frame);
 if(!rc)rc=demo_frame_from_mosaic(static_cast<const float*>(a.input),layout.width,layout.height,&source);
 if(!rc)rc=demo_store_run_asset(a.store,run->run_id,"frame-mosaic-input.f32",a.input,a.frame_bytes);
 if(!rc)rc=demo_store_run_asset(a.store,run->run_id,"frame-mosaic-input.rgb",source.pixels,source.bytes);
 if(!rc)rc=demo_store_run_asset(a.store,run->run_id,"frame-mosaic-output.f32",a.output,a.frame_bytes);
 if(!rc)rc=demo_store_run_asset(a.store,run->run_id,"frame-mosaic-output.rgb",frame.pixels,frame.bytes);
 if(!rc){auto *assets=json_object_new_array();uint32_t shape[4]={1,3,layout.height,layout.width},rgb[2]={layout.height,layout.width};artifact(assets,"frame-mosaic-input.f32","FP32","input",a.input,a.frame_bytes,shape,4);artifact(assets,"frame-mosaic-input.rgb","RGB8","input",source.pixels,source.bytes,rgb,2);artifact(assets,"frame-mosaic-output.f32","FP32","output",a.output,a.frame_bytes,shape,4);artifact(assets,"frame-mosaic-output.rgb","RGB8","display",frame.pixels,frame.bytes,rgb,2);json_object_object_add(o,"artifacts",assets);
  json_object_object_add(o,"state",json_object_new_string("RESULT_VALIDATED"));json_object_object_add(o,"numeric_class",json_object_new_string("NOT_EVALUATED"));json_object_object_add(o,"timing_valid",json_object_new_boolean(1));
#ifdef DEMO_SOFTWARE_MODEL_TEST
  json_object_object_add(o,"timing_valid",json_object_new_boolean(0));
  json_object_object_add(o,"timing_scope",json_object_new_string("HOST_PROTOCOL_MODEL_NOT_GPU_OR_BOARD_TIMING"));
  json_object_object_add(o,"cycle_valid",json_object_new_boolean(0));json_object_object_add(o,"raw_cycles",nullptr);
#else
  json_object_object_add(o,"cycle_valid",json_object_new_boolean(1));json_object_object_add(o,"raw_cycles",json_object_new_uint64(a.client.metrics.raw_cycles));
#endif
  auto *times=json_object_new_object();const auto &m=a.client.metrics;
  const char *keys[]={"load","input_copy","begin","gpu_submit_wait","evidence_reap","readback","end","inference_wall","nested_cdma","nested_guard","nested_immutable","nested_poll","nested_handoff"};uint64_t values[]={cold?m.cold_ns:0,m.input_ns,m.begin_ns,m.submit_wait_ns,m.evidence_ns,m.output_ns,m.end_ns,m.total_ns,m.io_ns,m.guard_ns,m.immutable_ns,m.poll_ns,m.handoff_ns};for(unsigned i=0;i<13;++i)json_object_object_add(times,keys[i],json_object_new_uint64(values[i]));json_object_object_add(o,"timings_ns",times);
  const char *text=json_object_to_json_string_ext(o,JSON_C_TO_STRING_PLAIN);rc=demo_parse_run(text,strlen(text),&a.result.run);if(!rc)demo_sha_hex(a.result.run.json,a.result.run.json_bytes,a.result.source_result_sha);
 }
 demo_frame_free(&frame);demo_frame_free(&source);json_object_put(o);if(rc)return first_error(a,*run,rc);
 if(a.serial==UINT64_MAX)return first_error(a,*run,-EOVERFLOW);
 *job=++a.serial;a.completed=1;return 0;
}
static int poll(void *p,int fd,uint64_t id){auto &a=*static_cast<demo_mosaic_adapter*>(p);return a.first_error?a.first_error:fd!=a.client.fd||id!=a.serial?-ESTALE:a.completed?1:0;}
static int collect(void *p,int fd,uint64_t id,demo_result *result){auto &a=*static_cast<demo_mosaic_adapter*>(p);if(!result||poll(p,fd,id)!=1)return -ESTALE;*result=a.result;return 0;}
int demo_mosaic_adapter_detach(void *p,int fd){auto &a=*static_cast<demo_mosaic_adapter*>(p);if(a.first_error)return a.first_error;if(!a.client.attached)return 0;if(a.client.fd!=fd)return -EBADF;return mosaic_client_close(&a.client);}
int demo_mosaic_ops(demo_mosaic_adapter *a,demo_gpu_ops *ops){if(!a||!ops)return -EINVAL;*ops={owner,release,start,poll,quiet,collect,a};return 0;}
int demo_mosaic_run(demo_session *s,const demo_run *r,demo_result *out){if(!s||!r||!out||r->app!=DEMO_MOSAIC)return -EINVAL;if(s->gpu.start!=start&&(!s->dispatcher||s->dispatcher->apps[0].start!=start||s->dispatcher->active!=DEMO_MOSAIC))return -EINVAL;int rc=demo_start(s,r);return rc?rc:demo_status(s,r->run_id,out);}

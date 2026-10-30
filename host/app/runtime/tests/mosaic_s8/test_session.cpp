#include "graph.h"
#include "fake_s8.h"
#include <vxl/mosaic_client.h>
#include <vxl/demo_mosaic.h>
#include <json-c/json.h>
#include <unistd.h>
#include <sys/stat.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <fstream>
#include <string>
#include <vector>
extern "C" {void mosaic_test_setup(const char *);void mosaic_test_cleanup(void);const mosaic_program *mosaic_test_program(void);void *mosaic_test_map(vxl_u64,vxl_u64);void mosaic_test_execute(int (*)(const mosaic_job_v1 *,unsigned));unsigned mosaic_test_submits(void);}
static std::vector<unsigned char> read_bytes(const std::string &path){std::ifstream f(path,std::ios::binary);assert(f);return std::vector<unsigned char>(std::istreambuf_iterator<char>(f),{});}
static uint32_t le32(const unsigned char *p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
static uint64_t le64(const unsigned char *p){return le32(p)|(uint64_t(le32(p+4))<<32);}
static mosaic_device_context device;static fake_s8 model[4];static unsigned calls;
static int execute(const mosaic_job_v1 *job,unsigned phase){device.phase_index=phase;device.expected=*job;++device.logical_jobs;for(unsigned core=0;core<4;++core){device.core=core;int rc=mosaic_execute_job(job,&device);if(rc)return rc;}++calls;return 0;}
int main(int argc,char **argv){
 assert(argc==3);auto assets=read_bytes(std::string(argv[1])+"/weights.ms8"),input=read_bytes(std::string(argv[2])+"/input.f32"),golden=read_bytes(std::string(argv[2])+"/stage15.f32");assert(input.size()==1152&&golden.size()==1152);
 auto *adapter=new demo_mosaic_adapter{};demo_gpu_ops adapter_ops{};assert(!demo_mosaic_ops(adapter,&adapter_ops));delete adapter;
 mosaic_test_setup((std::string(argv[1])+"/weights.ms8").c_str());const auto *program=mosaic_test_program();mosaic_client client;assert(!mosaic_client_attach_fd(&client,55));assert(!mosaic_client_configure(&client,program,assets.data(),assets.size(),0));assert(!mosaic_client_load(&client,program->assets));
 device={};device.width=12;device.height=8;device.layout=program->layouts[0].layout;auto &l=device.layout;auto &m=device.memory;
 auto map=[&](unsigned role)->void *{const auto &r=l.region[role];void *p=mosaic_test_map(r.address,r.bytes);assert(p);return p;};
 m.fp[0]=static_cast<float*>(map(MOSAIC_FP_A));m.fp[1]=static_cast<float*>(map(MOSAIC_FP_B));m.fp_count=l.region[MOSAIC_FP_A].bytes/4;m.quantized=static_cast<int16_t*>(map(MOSAIC_Q_INPUT));m.quantized_count=l.region[MOSAIC_Q_INPUT].bytes/2;m.skip=static_cast<float*>(map(MOSAIC_SKIP));m.skip_count=l.region[MOSAIC_SKIP].bytes/4;m.stats=static_cast<float*>(map(MOSAIC_STATS));m.stats_count=384;
 size_t tree=1;while(tree<96)tree*=2;auto *norm=static_cast<float*>(map(MOSAIC_NORM_TREE));auto *scratch=static_cast<unsigned char*>(map(MOSAIC_TILE_SCRATCH));size_t stride=l.region[MOSAIC_TILE_SCRATCH].bytes/4;m.tile_capacity=8*2592;
 for(unsigned core=0;core<4;++core){auto *s=scratch+core*stride;m.a[core]=reinterpret_cast<int16_t*>(s);m.b[core]=reinterpret_cast<int16_t*>(s+8*2592*2);m.tile[core]=reinterpret_cast<int64_t*>(s+2*8*2592*2);m.norm[core]={norm+core*tree*2,norm+(core*2+1)*tree,tree};device.io[core]=model[core].ops();device.io[core].core=core;}
 const auto *blob=static_cast<const unsigned char*>(mosaic_test_map(0x80800000,assets.size()));
 for(unsigned i=0;i<16;++i){const auto *r=blob+256+i*384;unsigned co=le32(r+4),kpad=le32(r+20);const float *coeff=reinterpret_cast<const float*>(blob+le64(r+136));float sa;memcpy(&sa,blob+le64(r+200),4);device.parameters[i]={reinterpret_cast<const int16_t*>(blob+le64(r+72)),size_t(co)*kpad,coeff,size_t(co)*2,sa,i==15?nullptr:reinterpret_cast<const float*>(blob+le64(r+264)),i==15?nullptr:reinterpret_cast<const float*>(blob+le64(r+328))};}
 mosaic_test_execute(execute);std::vector<unsigned char> output(1152),first;FILE *journal=tmpfile();assert(journal);assert(!mosaic_client_frame(&client,input.data(),input.size(),output.data(),output.size(),fileno(journal)));
 auto *run=new demo_run{};auto *result=new demo_result{};run->app=DEMO_MOSAIC;run->generation=9;strcpy(run->run_id,"55555555555555555555555555555555");
 assert(!mosaic_client_run(&client,run,result));assert(calls==77&&mosaic_test_submits()==77&&device.logical_jobs==77&&!device.diagnostics);assert(output==golden);first=output;
 strcpy(run->run_id,"66666666666666666666666666666666");assert(!mosaic_client_run(&client,run,result));assert(calls==154&&device.logical_jobs==154&&output==first&&client.epoch==2);assert(ftell(journal)==154*512);fclose(journal);
 char directory[]="/tmp/mosaic-adapter-test-XXXXXX";assert(mkdtemp(directory));demo_store store;assert(!demo_store_open(&store,directory,nullptr));auto *a=new demo_mosaic_adapter{};a->client=client;a->store=&store;a->program=program;a->assets=assets.data();a->assets_bytes=assets.size();a->input=input.data();a->output=output.data();a->frame_bytes=input.size();demo_gpu_ops gpu{};assert(!demo_mosaic_ops(a,&gpu));auto *session=new demo_session{};assert(!demo_session_init(session,&store,9,&gpu));session->owner_fd=55;
 for(unsigned frame=0;frame<2;++frame){auto *o=json_object_new_object(),*ids=json_object_new_object(),*shape=json_object_new_array();
  const char *keys[]={"hardware","source","program","model","input","quant","layout"};char input_hash[65];demo_sha_hex(input.data(),input.size(),input_hash);const vxl_u8 *digests[]={a->client.binding.digests[MOSAIC_HARDWARE_SHA],a->client.binding.digests[MOSAIC_SOURCE_SHA],a->client.binding.digests[MOSAIC_FLAT_SHA],a->client.binding.digests[MOSAIC_MODEL_SHA],nullptr,program->assets->package_sha,program->layouts[0].layout_sha};
  static const char hexchars[]="0123456789abcdef";for(unsigned k=0;k<7;++k){char h[65];if(k==4)strcpy(h,input_hash);else{for(unsigned x=0;x<32;++x){h[x*2]=hexchars[digests[k][x]>>4];h[x*2+1]=hexchars[digests[k][x]&15];}h[64]=0;}json_object_object_add(ids,keys[k],json_object_new_string(h));}
  for(int n:{1,3,8,12}){json_object_array_add(shape,json_object_new_int(n));}json_object_object_add(o,"shape",shape);json_object_object_add(o,"identities",ids);
  json_object_object_add(o,"schema",json_object_new_int(1));json_object_object_add(o,"app",json_object_new_string("mosaic"));json_object_object_add(o,"run_id",json_object_new_string(frame?"88888888888888888888888888888888":"77777777777777777777777777777777"));json_object_object_add(o,"boot_id",json_object_new_string("OFFLINE"));json_object_object_add(o,"build_id",json_object_new_string("OFFLINE-TEST"));json_object_object_add(o,"scope",json_object_new_string("OFFLINE"));json_object_object_add(o,"state",json_object_new_string("STAGED"));json_object_object_add(o,"numeric_class",json_object_new_string("NOT_EVALUATED"));json_object_object_add(o,"generation",json_object_new_int(9));json_object_object_add(o,"diagnostics",json_object_new_boolean(0));json_object_object_add(o,"cycle_valid",json_object_new_boolean(0));json_object_object_add(o,"timing_valid",json_object_new_boolean(0));json_object_object_add(o,"raw_cycles",nullptr);json_object_object_add(o,"artifacts",json_object_new_array());const char *text=json_object_to_json_string_ext(o,JSON_C_TO_STRING_PLAIN);assert(!demo_parse_run(text,strlen(text),run));json_object_put(o);
  unsigned before=mosaic_test_submits();assert(!demo_mosaic_run(session,run,result));assert(!demo_mosaic_run(session,run,result));assert(mosaic_test_submits()==before+77);assert(!demo_status(session,run->run_id,result));assert(result->run.state==DEMO_RESULT_VALIDATED&&!result->run.cycle_valid&&!result->run.timing_valid&&result->run.raw_cycles==0&&output==golden);assert(!demo_status(session,run->run_id,result)&&mosaic_test_submits()==before+77);
 }
 assert(a->client.epoch==4);assert(!mosaic_client_close(&a->client));demo_store_close(&store);delete a;delete session;mosaic_test_cleanup();delete run;delete result;
 puts("MOSAIC_CLIENT_ACTUAL_DRIVER_AND_16_LAYER_SMALL_SOFTWARE_PATH_TWO_FRAMES_BITEXACT_NO_DIAGNOSTICS_MODEL_COUNTERS_NOT_BOARD");return 0;
}

/* Production session compiled here to inject only syscall/data boundaries.
 * No product fixture option exists and no /dev/vortex0 is opened. */
#define S8_SESSION_TEST_ONLY 1
#include "../../tools/s8/session.cpp"
#include <cassert>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
static std::map<uint32_t,Bytes> expected_capture;
static unsigned reaps,fault;
static int mock_call(void *,int fd,vxl_u32 cmd,vxl_request *r){assert(fd==7);if(cmd==VXL_GET_INFO){r->profile_id=5;r->generation=2;r->state=1;}if(cmd==VXL_RESERVE){r->handle=r->address;r->gpu_address=uint32_t(r->address);}if(cmd==VXL_COPY_TO)r->used=r->length;if(cmd==VXL_SUBMIT)r->job_id=1;if(cmd==VXL_WAIT){r->state=3;r->result=0;}if(cmd==VXL_COPY_FROM){uint32_t gpu=uint32_t(r->handle+r->offset);auto it=expected_capture.find(gpu);assert(it!=expected_capture.end()&&it->second.size()==r->length);memcpy(reinterpret_cast<void *>(uintptr_t(r->user_ptr)),it->second.data(),r->length);r->used=r->length;}if(cmd==VXL_REAP)reaps++;return 0;}
static int mock_trace(void *p,const char *name,int post){auto &s=*static_cast<Session *>(p);try{std::string label=(post?"post-":"")+std::string(name);if(!strcmp(name,"binding.bin")){s.save(label,&s.binding,512);return 0;}
 std::map<std::string,std::string> row={{"job_id","1"},{"generation","2"},{"sequence","1"},{"state","3"},{"result","0"},{"error_class","0"},{"errors","0"},{"pending_w","0"},{"pending_r","0"},{"tex_pending","0"},{"timing_invalid","0"},{"exit0","0"},{"exit1","0"},{"exit2","0"},{"exit3","0"},{"elapsed_valid","1"},{"pre_ack","1"},{"pre_ack_ready","1"},{"pre_ack_idle","1"},{"ack_ready","1"},{"ack_idle","1"},{"guard_valid","1"},{"immutable_valid","1"},{"input_valid","1"},{"ack_valid","1"},{"identity_valid","1"},{"metadata_valid","1"},{"clock","1"},{"elapsed_address",std::to_string(0xa0000028u)},{"watchdog","4000000000"},{"exit_valid","15"},{"elapsed_raw","1234"},{"elapsed_u64","1234"},{"run_id",s.options.run_id},{"binding_tag",hex(s.submitted.binding_tag,16)}};
 row["jobs"]="1";row["jobs_before"]="0";row["cdma_status"]="4098";row["pre_ack_idle"]="0";row["status"]="1282";
 if(fault==3)row["errors"]="1";
 std::string header="#VXL_S8_TRACE_V1 overflow=0 active=0 failed=0 abandoned=0\n",keys,values;for(auto entry:row){if(!keys.empty()){keys+=',';values+=',';}keys+=entry.first;values+=entry.second;}std::string text=header+keys+"\n"+values+"\n";s.save(label,text.data(),text.size());return 0;}catch(int e){return e;}catch(...){return -EIO;}}
static void integrated(unsigned test_fault){
 fault=test_fault;reaps=0;expected_capture.clear();Session s;s.options.run_id="01000000000000000000000000000000";char path[]="build/s8/session-integrated-XXXXXX";assert(mkdtemp(path));char *absolute=realpath(path,nullptr);assert(absolute);s.options.evidence=absolute;free(absolute);s.directory=open(path,O_RDONLY|O_DIRECTORY);assert(s.directory>=0);unsigned case_id=test_fault>=4?2:1;
 s.layout=Json{{"elf",{{"tls_base",0x80002000},{"tbss_stride",16}}}};
 for(unsigned i=0;i<53;++i){assert(!vxl_s8_object_contract(i,&s.client.objects[i]));s.initial[i].resize(s.client.objects[i].bytes,0);s.client.objects[i].initial=s.initial[i].data();}
 auto take=[](uint32_t gpu,unsigned bytes){assert(expected_capture.emplace(gpu,Bytes(bytes,0)).second);};
 for(unsigned i=0;i<16;++i)take(0x81400000+i*4096,4096);
 take(0x81410000,4096);take(0x81411000,4096);take(0x800f0000,64);take(0x800f0040,64);take(0x800fffc0,64);for(unsigned h=0;h<64;++h)take(0xffff0000u-(h+1)*8192,64);take(0xffff0000,64);for(unsigned which=0;which<2;++which)for(unsigned slot=0;slot<16;++slot){uint32_t a=(which?0x81200000:0x81000000)+slot*0x20000;take(a,64);take(a+64+(case_id==2?256:0),64);take(a+0x1ffc0,64);}
 auto &identity=expected_capture.at(0x81410000);for(unsigned h=0;h<64;++h){vxl_s8_identity_v1 id={h/16,h%16/4,h%4,h,0xffff0000u-h*8192,0x80002000u+h*16,VXL_S8_STACK_TAG^h,VXL_S8_TLS_TAG^h};memcpy(identity.data()+64+h*32,&id,32);}for(unsigned c=0;c<4;++c){vxl_s8_core_done done={VXL_S8_CORE_MAGIC,1,c,2,1,0,16,VXL_S8_DONE};memcpy(identity.data()+0x840+c*32,&done,32);}vxl_s8_global_done global={};global.magic=VXL_S8_GLOBAL_MAGIC;global.version=1;global.bytes=64;global.generation=2;global.sequence=1;global.cores=15;global.harts=64;global.done=VXL_S8_DONE;global.run_id[0]=1;global.binding_tag[0]=2;memcpy(identity.data()+0x8c0,&global,64);
 if(fault==1)identity[64+16]^=1;
 if(fault==2)expected_capture.at(0x800f0000)[0]^=1;
 if(case_id==2){auto &out=expected_capture.at(0x81400000);for(unsigned i=0;i<64;++i){uint32_t value=63;memcpy(out.data()+64+i*4,&value,4);}vxl_s8_result_v1 m={};m.magic=VXL_S8_RESULT_MAGIC;m.version=1;m.bytes=256;m.case_id=2;m.generation=2;m.sequence=1;m.valid_bits=S8_RAW_VALID;m.format=2;m.primary_elements=64;m.done=VXL_S8_DONE;m.run_id[0]=1;m.binding_tag[0]=2;memcpy(out.data()+0x3c0,&m,256);if(test_fault==4)out[64]^=1;}
 s.client.fd=7;s.client.io={&s,mock_call,Session::saved,mock_trace,Session::sealed,Session::now,Session::event};assert(!vxl_s8_client_bind(&s.client));assert(!vxl_s8_client_prepare(&s.client));vxl_s8_job_v1 j={};j.magic=VXL_S8_JOB_MAGIC;j.version=1;j.bytes=128;j.case_id=case_id;j.core_mask=1;j.run_id[0]=1;j.binding_tag[0]=2;
 int rc=vxl_s8_run_case(7,&j,s.options.evidence.c_str());assert(((test_fault==0||test_fault==5)&&rc==0&&reaps==1)||((test_fault!=0&&test_fault!=5)&&rc<0&&!reaps));s.client.fd=-1;
}
int main(){
 S8Options o;const char *missing[]={"vxl_s8","--manifest","x"};assert(s8_parse_options(3,missing,&o)<0);
 const char *args[]={"vxl_s8","--manifest","m","--catalog","c","--case","4","--run-id","0123456789abcdef0123456789abcdef","--evidence","/run/vxl-s8-explicit"};assert(!s8_parse_options(11,args,&o)&&o.case_id==4&&!o.flags);
 const char *duplicate[]={"vxl_s8","--case","4","--case","5"};assert(s8_parse_options(5,duplicate,&o)<0);
 char name[]="build/s8/session-XXXXXX";assert(mkdtemp(name));int fd=open(name,O_RDONLY|O_DIRECTORY);assert(fd>=0);const char text[]="original evidence";
 assert(!s8_persist(fd,"raw.bin",text,sizeof(text)));assert(s8_persist(fd,"raw.bin","changed",7)==-EEXIST);assert(s8_persist(fd,"../escape.bin",text,sizeof(text))<0);
 int f=openat(fd,"raw.bin",O_RDONLY);char got[64]={0};assert(read(f,got,sizeof(got))==sizeof(text)&&!memcmp(got,text,sizeof(text)));close(f);close(fd);
 o.manifest="/missing-explicit-s8-release/manifest.json";assert(s8_session_run_selected(o)!=0); // no valid manifest: no device open
 for(unsigned failure=0;failure<6;++failure)integrated(failure);
 puts("S8_SESSION_EXPLICIT_ARGS_DURABLE_EXCLUSIVE_NO_OVERWRITE_PREFLIGHT_PASS SOFTWARE_ONLY");
}

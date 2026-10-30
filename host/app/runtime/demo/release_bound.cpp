/* Final provider: real adapters and immutable data; no device opened on install. */
#include <vxl/demo_release.h>
#include <vxl/demo_dual_runtime.h>
#include <vxl/demo_frame.h>
#include <vxl/hunet.h>
#include "release_io.h"
#include <demo/mosaic_release_template.h>
#include <demo/release_assets.h>
#include <array>
#include <vector>
#include <string>
#include <memory>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace {
struct File {int fd=-1;~File(){if(fd>=0)close(fd);}};
struct State {
 std::array<std::vector<uint8_t>,DEMO_RELEASE_FILE_COUNT> blobs;
 std::vector<uint8_t> mosaic_binding,hunet_binding,gate;
 std::vector<float> input,output,roi,history;
 std::string hunet_root,probe;
 mosaic_program program{};demo_labels labels{};demo_frame frame{};
 demo_hunet_config hunet{};demo_dual_config config{};demo_dual_runtime runtime{};
 ~State(){demo_labels_close(&labels);}
};
std::unique_ptr<State> installed;
int uninstall(demo_session *s){
 if(!s||!installed||installed->runtime.dispatch.session!=s)return -ESTALE;
 int rc=demo_dual_runtime_destroy(&installed->runtime);if(rc)return rc;
 s->release_provider_close=nullptr;installed.reset();return 0;
}
uid_t owner(){
#ifdef DEMO_RELEASE_HOST_TEST
 return geteuid();
#else
 return 0;
#endif
}
void need(int rc){if(rc)throw rc;}
void require(bool yes,int rc=-ESTALE){if(!yes)throw rc;}
std::string system_identity(const char *path){
 File file;file.fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(file.fd<0)throw -errno;
 struct stat st{};if(fstat(file.fd,&st))throw -errno;
 require(S_ISREG(st.st_mode)&&st.st_uid==0&&!(st.st_mode&0022),-EPERM);
 char text[130];size_t used=0;
 while(used<sizeof(text)){ssize_t n=read(file.fd,text+used,sizeof(text)-used);if(n<0&&errno==EINTR)continue;if(n<0)throw -errno;if(!n)break;used+=size_t(n);}
 require(used&&used<sizeof(text),-EINVAL);if(text[used-1]=='\n')--used;require(used&&used<=128,-EINVAL);
 for(size_t i=0;i<used;i++)require(uint8_t(text[i])>=33&&uint8_t(text[i])<=126,-EINVAL);
 return std::string(text,used);
}
void self_sha(uint8_t sha[32]){
 File f;f.fd=open("/proc/self/exe",O_RDONLY|O_CLOEXEC);if(f.fd<0)throw -errno;
 struct stat before{},after{};if(fstat(f.fd,&before))throw -errno;
 require(S_ISREG(before.st_mode)&&before.st_uid==owner()&&!(before.st_mode&0022)&&before.st_size>0&&uint64_t(before.st_size)<=67108864,-EPERM);
 vxl_hunet_sha state;vxl_hunet_sha_init(&state);uint8_t buffer[16384];uint64_t total=0;
 while(true){ssize_t n=read(f.fd,buffer,sizeof(buffer));if(n<0&&errno==EINTR)continue;if(n<0)throw -errno;if(!n)break;total+=uint64_t(n);require(total<=uint64_t(before.st_size));vxl_hunet_sha_update(&state,buffer,size_t(n));}
 if(fstat(f.fd,&after))throw -errno;
 require(total==uint64_t(before.st_size)&&before.st_dev==after.st_dev&&before.st_ino==after.st_ino&&before.st_size==after.st_size&&before.st_mtim.tv_sec==after.st_mtim.tv_sec&&before.st_mtim.tv_nsec==after.st_mtim.tv_nsec&&before.st_ctim.tv_sec==after.st_ctim.tv_sec&&before.st_ctim.tv_nsec==after.st_ctim.tv_nsec);
 vxl_hunet_sha_final(&state,sha);
}
std::vector<uint8_t> &blob(State &s,const char *name){for(unsigned i=0;i<DEMO_RELEASE_FILE_COUNT;++i)if(!strcmp(demo_release_files[i].path,name))return s.blobs[i];throw -ENOENT;}
void floats(std::vector<float> &out,const std::vector<uint8_t> &in){require(!(in.size()%4));out.resize(in.size()/4);memcpy(out.data(),in.data(),in.size());}
void copy_sha(uint8_t out[32],const std::vector<uint8_t> &v){vxl_hunet_hash(v.data(),v.size(),out);}
void prepare(State &s,demo_store *store,const char *root,int fd){
 for(unsigned i=0;i<DEMO_RELEASE_FILE_COUNT;++i){const auto &r=demo_release_files[i];need(demo_release_read(fd,r.path,r.bytes,r.sha,owner(),s.blobs[i]));}
 uint8_t cpu[32];self_sha(cpu);std::array<uint8_t,512> expected;
 memcpy(expected.data(),demo_release_mosaic_binding_template,512);memcpy(expected.data()+320,cpu,32);
 need(demo_release_read(fd,"mosaic/binding.bin",512,nullptr,owner(),s.mosaic_binding));require(!memcmp(expected.data(),s.mosaic_binding.data(),512));
 memcpy(expected.data(),demo_release_hunet_binding_template,512);memcpy(expected.data()+256,cpu,32);
 need(demo_release_read(fd,"hunet/binding-fused.bin",512,nullptr,owner(),s.hunet_binding));require(!memcmp(expected.data(),s.hunet_binding.data(),512));
 need(demo_release_read(fd,"hunet/numeric-gate.bin",128,nullptr,owner(),s.gate));uint8_t binding_sha[32];copy_sha(binding_sha,s.hunet_binding);
 require(!memcmp(s.gate.data(),"VXFUSG1",8)&&!memcmp(s.gate.data()+16,binding_sha,32));
 uint32_t flags,roi;memcpy(&flags,s.gate.data()+8,4);memcpy(&roi,s.gate.data()+12,4);require(roi==0&&(flags==0||flags==7));
 unsigned audit=0,run=0,tail=0;for(unsigned i=48;i<80;i++)audit|=s.gate[i];for(unsigned i=80;i<96;i++)run|=s.gate[i];for(unsigned i=96;i<128;i++)tail|=s.gate[i];
 require(!tail&&(flags==0?(!audit&&!run):(audit&&run)));
 // Same executable required by the existing controlled HUNet loader. The
 // compiled file manifest deliberately excludes this self-dependent payload.
 std::vector<uint8_t> hn_cpu;struct stat self{};File executable;executable.fd=open("/proc/self/exe",O_RDONLY|O_CLOEXEC);if(executable.fd<0)throw -errno;if(fstat(executable.fd,&self))throw -errno;
 need(demo_release_read(fd,"hunet/cpu/gelu.elf",size_t(self.st_size),cpu,owner(),hn_cpu));
 demo_release_mosaic_template(&s.program);auto &flat=blob(s,"mosaic/program12/code.bin");auto &ro=blob(s,"mosaic/program12/norm-rodata.bin");auto &assets=blob(s,"mosaic/weights.ms8");
 s.program.flat=flat.data();s.program.rodata=ro.data();memcpy(&s.program.binding,s.mosaic_binding.data(),512);copy_sha(s.program.binding_sha,s.mosaic_binding);require(!memcmp(s.program.binding.digests[MOSAIC_SOURCE_SHA],demo_release_source_sha,32));
 vxl_engine e{};e.profile_id=6;mosaic_ctx c{};mosaic_host_state h{};need(mosaic_host_init(&c,&h,&e,s.program.binding_sha));need(mosaic_program_register(&c,&s.program));need(mosaic_assets_validate(assets.data(),assets.size(),s.program.assets));
 auto &in=blob(s,"mosaic/input.f32");floats(s.input,in);s.output.resize(s.input.size());const auto &layout=s.program.layouts[DEMO_RELEASE_MOSAIC_DEFAULT_LAYOUT].layout;require(layout.width==1280&&layout.height==960&&in.size()==size_t(layout.width)*layout.height*12);
 auto &table=blob(s,"display/hunet-labels.bin");auto &atlas=blob(s,"display/hunet-glyphs-u8.bin");need(demo_labels_bind(table.data(),table.size(),atlas.data(),atlas.size(),demo_release_classes_sha,demo_release_font_sha,&s.labels));s.labels.bbox={40,12,87,105,474,345,0};
 auto &rgb=blob(s,"display/hunet-source-roi0.rgb");require(rgb.size()==474u*345u*3u);s.frame.width=474;s.frame.height=345;s.frame.stride=1422;s.frame.bytes=rgb.size();s.frame.pixels=rgb.data();need(demo_frame_hash(&s.frame));strcpy(s.frame.raw_source_sha,s.frame.sha256);
 floats(s.roi,blob(s,"hunet/input-roi0.f32"));floats(s.history,blob(s,"display/hunet-historical-logits.f32"));auto &probe=blob(s,"display/hunet-historical-probe.json");require(probe.size()&&probe.size()<=DEMO_MAX_JSON);s.probe.assign(reinterpret_cast<const char*>(probe.data()),probe.size());require(s.probe.find('\0')==std::string::npos);
 s.hunet_root=std::string(root)+"/hunet";auto &hn=s.hunet;hn.store=store;hn.release_root=s.hunet_root.c_str();hn.input=s.roi.data();hn.input_bytes=s.roi.size()*4;hn.roi=0;hn.labels=&s.labels;hn.source_frame=&s.frame;hn.historical=s.history.data();hn.historical_count=s.history.size();hn.historical_ort_pass=0;hn.historical_r2_pass=1;hn.historical_probe_json=s.probe.c_str();hn.historical_probe_bytes=probe.size();copy_sha(hn.historical_probe_sha,probe);copy_sha(hn.historical_logits_sha,blob(s,"display/hunet-historical-logits.f32"));copy_sha(hn.historical_input_sha,blob(s,"hunet/input-roi0.f32"));memcpy(hn.historical_model_sha,s.hunet_binding.data()+224,32);memcpy(hn.hardware_sha,s.program.binding.digests[MOSAIC_HARDWARE_SHA],32);memcpy(hn.libm_sha,demo_release_libm_sha,32);hn.gate=s.gate.data();hn.gate_bytes=128;
 s.config={store,&s.program,assets.data(),s.input.data(),s.output.data(),assets.size(),in.size(),DEMO_RELEASE_MOSAIC_DEFAULT_LAYOUT,&hn,"/run/vxl-demo/dp.sock"};
}
}
extern "C" int demo_release_install(demo_session *session,demo_store *store,const char *root){
 if(!session||!store||!root||root[0]!='/')return -EINVAL;
 if(installed)return -EBUSY;
 try{
  File directory;directory.fd=demo_release_open_root(root,owner());if(directory.fd<0)return directory.fd;
  auto boot=system_identity("/proc/sys/kernel/random/boot_id"),build=system_identity("/etc/codex-build-id");
  auto s=std::unique_ptr<State>(new State);prepare(*s,store,root,directory.fd);need(demo_dual_runtime_init(&s->runtime,session,&s->config));
  need(demo_session_bind_identity(session,boot.c_str(),build.c_str()));installed=std::move(s);session->release_provider_close=uninstall;return 0;
 }catch(int rc){return rc;}catch(...){return -ENOMEM;}
}

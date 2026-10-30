#include <vxunified/joint_bridge.hpp>
#include <vxl/hunet.h>
#include <cassert>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
using namespace vxunified;
static Digest literal(const char*s){Digest d{};for(unsigned i=0;i<32;i++){auto n=[](char c){return unsigned(c<='9'?c-'0':c-'a'+10);};d[i]=(n(s[i*2])<<4)|n(s[i*2+1]);}return d;}
static Digest hash(const std::string&s){Digest d{};vxl_hunet_hash(s.data(),s.size(),d.data());return d;}
static std::string hex(Digest d){std::string s;for(auto b:d){s+="0123456789abcdef"[b>>4];s+="0123456789abcdef"[b&15];}return s;}
static void directory(const std::string&p){assert(mkdir(p.c_str(),0700)==0);}
static void file(const std::string&p,const std::string&s){if(access(p.c_str(),F_OK)==0)assert(chmod(p.c_str(),0600)==0);std::ofstream f(p,std::ios::binary);assert(f.good());f<<s;f.close();assert(chmod(p.c_str(),0400)==0);}
int main(){
 char temp[]="/tmp/vx-joint-bridge-XXXXXX";assert(mkdtemp(temp));StartupContext c;c.private_dir=temp;c.boot_id="boot";c.scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;
 auto&h=c.identity;h.provider_flags=4;h.device_generation=1;h.hardware_contract=literal("1bf11d26789e7c602f9c05a5ecc97e2605ac7807deade8f4bfc549487f45ec5d");h.bit=literal("94ae9d3e6eb73ba55a8eec8c802e3818cd6b3a9be7e60542da5ae9459e3579e1");h.xsa=literal("02e68160e21b01621fde1eac6e472810e0fe50cd1842acb3ab63469314355e94");h.compatible=literal("f670c653a37361f05f57c14eecd53dea1b87556489a2c1314efd208d715826dc");
 PublishedResult p;p.execution_complete=true;p.lease.producer={"boot","client","original"};p.lease.app=AppKind::NN;p.lease.scope=c.scope;p.lease.hardware=h.hardware_contract;p.binding.emplace();auto&b=*p.binding;b.hardware=h.hardware_contract;b.bit=h.bit;b.xsa=h.xsa;b.compatible=h.compatible;b.provider_flags=4;b.device_generation=1;b.model=hash("model");b.plan=hash("plan");b.input=hash("input");b.source=hash("source");p.lease.model=b.model;p.lease.plan=b.plan;
 ResultArtifact output;output.path="original/output.f32";output.sha=hash("original tensor");output.bytes=12;output.shape={3,1,1};p.lease.artifacts={output};
 NativeResultExport saved;saved.manifest.path="export/mosaic-result.json";saved.manifest.layout="JSON_UTF8";const std::string original="HOST_METADATA_PATH_BINDING_ONLY_NOT_A_NUMERIC_OR_GPU_RESULT";saved.manifest.sha=hash(original);saved.manifest.bytes=original.size();saved.tensor=output;saved.tensor.path="export/output.f32";saved.tensor.layout="CHW_F32";
 directory(c.private_dir+"/outputs");directory(c.private_dir+"/outputs/export");file(c.private_dir+"/outputs/export/mosaic-result.json",original);
 directory(c.private_dir+"/texture-inbox");const auto inbox=c.private_dir+"/texture-inbox/"+hex(saved.manifest.sha);directory(inbox);
 const auto package=hash("unverified-package-body-for-path-binding-test");const auto meta="{\"package_id\":\""+hex(package)+"\",\"source\":{\"result_sha256\":\""+hex(saved.manifest.sha)+"\",\"tensor_sha256\":\""+hex(saved.tensor.sha)+"\",\"scope\":\"OFFLINE\"}}";file(inbox+"/manifest.json",meta);
 auto bound=joint_texture_file(c,p,saved,package);if(!bound)std::fprintf(stderr,"JOINT_BRIDGE_ERROR code=%d category=%s detail=%s\n",bound.error->code,bound.error->category.c_str(),bound.error->detail.c_str());assert(bound&&bound.value->id==hex(saved.manifest.sha)&&bound.value->result_path==c.private_dir+"/outputs/export/mosaic-result.json"&&bound.value->policy.package_directory==inbox&&bound.value->policy.expected_package_id==package);
 auto changed=p;changed.lease.producer.boot_id="historical";assert(!joint_texture_file(c,changed,saved,package));changed=p;changed.binding->bit[0]^=1;assert(!joint_texture_file(c,changed,saved,package));changed=p;changed.execution_complete=false;assert(!joint_texture_file(c,changed,saved,package));
 auto escaped=saved;escaped.manifest.path="../export/mosaic-result.json";assert(!joint_texture_file(c,p,escaped,package));escaped=saved;escaped.tensor.sha[0]^=1;assert(!joint_texture_file(c,p,escaped,package));
 file(inbox+"/manifest.json","{\"package_id\":\""+hex(package)+"\",\"source\":{\"result_sha256\":\""+hex(hash("history"))+"\",\"tensor_sha256\":\""+hex(saved.tensor.sha)+"\",\"scope\":\"OFFLINE\"}}");assert(!joint_texture_file(c,p,saved,package));
 // A self-consistent replacement package is not the independently frozen
 // B conversion result, even when it keeps this boot's exact source hashes.
 file(inbox+"/manifest.json","{\"package_id\":\""+hex(hash("historical LODs with new self-consistent digest"))+"\",\"source\":{\"result_sha256\":\""+hex(saved.manifest.sha)+"\",\"tensor_sha256\":\""+hex(saved.tensor.sha)+"\",\"scope\":\"OFFLINE\"}}");assert(!joint_texture_file(c,p,saved,package));
 file(inbox+"/manifest.json",meta);assert(!unlink((inbox+"/manifest.json").c_str()));assert(!symlink("/etc/passwd",(inbox+"/manifest.json").c_str()));assert(!joint_texture_file(c,p,saved,package));
 puts("JOINT_LIVE_EXPORT_FIXED_PATH_SOURCE_DIGEST_BOOT_SCOPE_BINDING_PASS_NOT_GPU_OR_PIXELS");
}

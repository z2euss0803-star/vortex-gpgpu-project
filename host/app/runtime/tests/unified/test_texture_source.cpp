#include <vxunified/texture.hpp>
#include <vxl/hunet.h>
#include <json-c/json.h>
#include <fstream>
#include <cstring>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>
using namespace vxunified;
namespace {
void need(bool yes,const char*text){if(!yes){std::fprintf(stderr,"U17_TEXTURE_ASSERT %s\n",text);std::exit(1);}}
Digest digest(const std::vector<uint8_t>&b){Digest d{};vxl_hunet_hash(b.data(),b.size(),d.data());return d;}
std::string hex(Digest d){std::string s;for(auto b:d){s+="0123456789abcdef"[b>>4];s+="0123456789abcdef"[b&15];}return s;}
void save(const std::string&p,const std::vector<uint8_t>&b){std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),b.size());need(bool(f),"fixture write");}
void save(const std::string&p,const std::string&s){save(p,std::vector<uint8_t>(s.begin(),s.end()));}
}
int main(){
 char name[]="/tmp/vx-texture-source-XXXXXX";need(mkdtemp(name)!=nullptr,"private fixture directory");std::string dir=name;
 std::ifstream f("tests/demo/fixtures/run-v1.json",std::ios::binary);std::string template_json{std::istreambuf_iterator<char>(f),{}};need(!template_json.empty(),"frozen result contract fixture");
 constexpr uint32_t w=1280,h=960;const size_t pixels=size_t(w)*h;std::vector<uint8_t>raw(pixels*12);for(unsigned c=0;c<3;c++){float v=c==0?-1.0f:c==1?127.75f:300.0f;for(size_t i=0;i<pixels;i++)std::memcpy(raw.data()+4*(c*pixels+i),&v,4);}
 auto json=[&](const std::string&state,const std::string&asset,const std::vector<uint8_t>&bytes){auto*o=json_tokener_parse(template_json.c_str());need(o!=nullptr,"fixture JSON");json_object_object_add(o,"state",json_object_new_string(state.c_str()));auto*shape=json_object_new_array();for(unsigned n:{1u,3u,h,w})json_object_array_add(shape,json_object_new_int64(n));json_object_object_add(o,"shape",shape);
 auto*rows=json_object_new_array(),*row=json_object_new_object();json_object_object_add(row,"role",json_object_new_string("output"));json_object_object_add(row,"name",json_object_new_string(asset.c_str()));json_object_object_add(row,"dtype",json_object_new_string("FP32"));json_object_object_add(row,"bytes",json_object_new_uint64(bytes.size()));json_object_object_add(row,"sha256",json_object_new_string(hex(digest(bytes)).c_str()));json_object_object_add(row,"shape",json_object_get(shape));json_object_array_add(rows,row);json_object_object_add(o,"artifacts",rows);std::string text=json_object_to_json_string_ext(o,JSON_C_TO_STRING_PLAIN);json_object_put(o);return text;};
 auto text=json("RESULT_VALIDATED","output.f32",raw);save(dir+"/output.f32",raw);save(dir+"/result.json",text);auto admitted=import_result(dir+"/result.json");need(bool(admitted),"complete Mosaic file must import");const auto&a=*admitted.value;
 need(a.lease.app==AppKind::NN&&a.lease.scope==vxnn::EvidenceScope::OFFLINE&&a.raw_fp32==raw&&a.tensor_sha==digest(raw)&&a.original_json==text&&a.numeric_class=="NOT_EVALUATED","source evidence and original tensor retained");need(a.width==w&&a.height==h&&a.rgb.size()==pixels*3,"CHW to RGB extent");for(size_t i=0;i<pixels;i++)need(a.rgb[3*i]==0&&a.rgb[3*i+1]==127&&a.rgb[3*i+2]==255,"original truncate clamp semantics");
 save(dir+"/result.json",json("RUNNING","output.f32",raw));need(!import_result(dir+"/result.json"),"incomplete result refused");save(dir+"/result.json",text);raw[0]^=1;save(dir+"/output.f32",raw);need(!import_result(dir+"/result.json"),"hash drift refused");raw[0]^=1;
 uint32_t infinity=0x7f800000u;std::memcpy(raw.data(),&infinity,4);save(dir+"/output.f32",raw);save(dir+"/result.json",json("RESULT_VALIDATED","output.f32",raw));need(!import_result(dir+"/result.json"),"nonfinite output refused even with matching SHA");float original=-1;std::memcpy(raw.data(),&original,4);save(dir+"/output.f32",raw);
 save(dir+"/result.json",json("RESULT_VALIDATED","../output.f32",raw));need(!import_result(dir+"/result.json"),"path escape refused");need(!symlink("output.f32",(dir+"/alias.f32").c_str()),"symlink fixture");save(dir+"/result.json",json("RESULT_VALIDATED","alias.f32",raw));need(!import_result(dir+"/result.json"),"symlink asset refused");save(dir+"/result.json",text.substr(0,text.size()-1)+",\"scope\":\"REAL_BOARD\"}");need(!import_result(dir+"/result.json"),"duplicate scope key refused");
 unlink((dir+"/alias.f32").c_str());unlink((dir+"/output.f32").c_str());unlink((dir+"/result.json").c_str());rmdir(dir.c_str());puts("U17_MOSAIC_SOURCE_IMPORT_FINITE_HASH_SCOPE_AND_RGB_PASS_HOST_NOT_GPU_DP gpu_raw_cycles=null");return 0;
}

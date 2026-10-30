#include <vxunified/installation.hpp>
#include "scheduler_fixture.hpp"
#include "nnrt/package/json.hpp"
#include <sstream>
#include <type_traits>
namespace {
BackendIdentity hardware(){BackendIdentity h;h.hardware_contract=digest(1);h.bit=digest(2);h.xsa=digest(3);h.compatible=digest(4);h.device_generation=1;h.provider_flags=1;return h;}
std::string hex(unsigned n){return vxnn::digest_hex(digest(uint8_t(n)));}
std::string manifest(const char*kind,bool presets,bool old=false){
 std::ostringstream s;s<<"{\"version\":"<<(old?1:2)<<",\"hardware\":{\"hardware_contract\":\""<<hex(1)<<"\",\"bit\":\""<<hex(2)<<"\",\"xsa\":\""<<hex(3)<<"\",\"compatible\":\""<<hex(4)<<"\",\"provider_flags\":1},\"catalog\":{\"path\":\"/opt/vxgpu/catalog.bin\",\"sha256\":\""<<hex(5)<<"\"},\"nn\":[{\"id\":\"native\",\"input_id\":\"frozen\",\"kind\":\""<<kind<<"\",\"package\":\"/opt/vxgpu/package\",\"model\":\""<<hex(6)<<"\",\"plan\":\""<<hex(7)<<"\",\"input\":\""<<hex(8)<<"\",\"resident_bytes\":2097152,\"input_bytes\":196608,\"job_budget_ns\":60000000000,\"family\":{\"code\":\""<<hex(9)<<"\",\"schema\":\""<<hex(10)<<"\",\"implementation\":\""<<hex(11)<<'"';
 if(std::string(kind)=="mosaic")s<<",\"readonly\":\""<<hex(12)<<'"';s<<'}';
 if(presets)s<<",\"presets\":{\"directory\":\"/opt/vxgpu/hunet-presets\",\"sha256\":\""<<hex(13)<<"\"}";s<<"}]";if(!old)s<<",\"display\":null,\"image\":null,\"p8\":null";s<<'}';return s.str();
}
auto decode(const std::string&s){return decode_native_installation(s,hardware(),vxnn::content_digest(s));}
template<class T,class=void>struct Presets {static void check(const T&){require(false,"typed installed optional presets required");}};
template<class T>struct Presets<T,std::void_t<decltype(std::declval<T>().presets->directory),decltype(std::declval<T>().presets->manifest)>>{static void check(const T&t){require(t.presets&&t.presets->directory=="/opt/vxgpu/hunet-presets"&&t.presets->manifest==digest(13),"exact optional administrator presets binding");}};
}
int main(){
 require(bool(decode(manifest("hunet",false,true)))&&bool(decode(manifest("hunet",false)))&&bool(decode(manifest("mosaic",false))),"old NN installation manifests remain compatible");
 auto fixed=decode(manifest("hunet",true));require(bool(fixed),"sealed optional HUNet presets must decode");Presets<InstalledNnModel>::check(fixed.value->nn.at(0));
 require(!decode(manifest("mosaic",true)),"Mosaic cannot claim HUNet presets");
 auto root=vxnn::json::parse(manifest("hunet",true));auto*model=vxnn::json::at(vxnn::json::member(root.get(),"nn"),0);auto*p=vxnn::json::member(model,"presets");
 for(const auto*path:{"relative","/opt/../escape","/opt//alias","/opt/trailing/"}){json_object_object_add(p,"directory",json_object_new_string(path));require(!decode(vxnn::json::dump(root.get())),"noncanonical preset directory rejected");}
 json_object_object_add(p,"directory",json_object_new_string("/opt/vxgpu/hunet-presets"));json_object_object_add(p,"sha256",json_object_new_string(std::string(64,'0').c_str()));require(!decode(vxnn::json::dump(root.get())),"zero preset manifest SHA rejected");
 json_object_object_add(p,"sha256",json_object_new_string(hex(13).c_str()));json_object_object_add(p,"raw_file",json_object_new_string("untrusted"));require(!decode(vxnn::json::dump(root.get())),"unknown preset fields rejected");
 std::puts("R5_HUNET_PRESETS_INSTALLATION_OLD_COMPAT_HUNET_ONLY_SEALED_DECLARATION_PASS_HOST_NOT_GPU");
}

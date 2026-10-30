#include <vxunified/installation.hpp>
#include "scheduler_fixture.hpp"
#include "nnrt/package/json.hpp"
#include <sstream>
#include <type_traits>
namespace {
BackendIdentity hardware(uint32_t provider){BackendIdentity h;h.hardware_contract=digest(1);h.bit=digest(2);h.xsa=digest(3);h.compatible=digest(4);h.device_generation=1;h.provider_flags=provider;return h;}
std::string hex(unsigned n){return vxnn::digest_hex(digest(uint8_t(n)));}
std::string model(const std::string&id,const std::string&kind){
 std::ostringstream s;s<<"{\"id\":\""<<id<<"\",\"input_id\":\"frozen\",\"kind\":\""<<kind<<"\",\"package\":\"/opt/vxgpu/"<<id<<"\",\"model\":\""<<hex(6)<<"\",\"plan\":\""<<hex(7)<<"\",\"input\":\""<<hex(8)<<"\",\"resident_bytes\":2097152,\"input_bytes\":4,\"job_budget_ns\":60000000000,\"family\":{\"code\":\""<<hex(9)<<"\",\"schema\":\""<<hex(10)<<"\",\"implementation\":\""<<hex(11)<<'"';if(kind=="mosaic"){s<<",\"readonly\":\""<<hex(12)<<'"';}s<<"}}";return s.str();
}
std::string manifest(uint32_t provider,bool image,bool p8,bool nn=false){
 std::ostringstream s;s<<"{\"version\":2,\"hardware\":{\"hardware_contract\":\""<<hex(1)<<"\",\"bit\":\""<<hex(2)<<"\",\"xsa\":\""<<hex(3)<<"\",\"compatible\":\""<<hex(4)<<"\",\"provider_flags\":"<<provider<<"},\"catalog\":{\"path\":\"/opt/vxgpu/catalog.bin\",\"sha256\":\""<<hex(5)<<"\"},\"nn\":[";
 if(nn)s<<model("mosaic","mosaic")<<','<<model("hunet","hunet");
 s<<"],\"display\":{\"device\":\"/dev/dri/card0\"},\"image\":";
 if(image)s<<"{\"implementation\":\""<<hex(13)<<"\",\"reference\":\""<<hex(14)<<"\",\"readonly\":{\"path\":\"/opt/vxgpu/image/tables.bin\",\"sha256\":\""<<hex(15)<<"\"},\"session\":42,\"bundle\":\""<<hex(16)<<"\",\"display_base\":100,\"port\":5011,\"address\":\"0.0.0.0\",\"job_budget_ns\":60000000000}";else s<<"null";
 s<<",\"p8\":";if(p8)s<<"{\"assets_directory\":\"/opt/vxgpu/p8\",\"assets\":\""<<hex(17)<<"\",\"session\":77,\"display_base\":200,\"port\":5007,\"address\":\"0.0.0.0\",\"job_budget_ns\":60000000000}";else s<<"null";s<<'}';return s.str();
}
Result<NativeInstallation>decode(const std::string&s,uint32_t p){return decode_native_installation(s,hardware(p),vxnn::content_digest(s));}
template<class T,class=void>struct Fields{static void check(const T&,bool,bool){require(false,"U20 typed native installation media/display bindings required");}};
template<class T>struct Fields<T,std::void_t<decltype(std::declval<T>().version),decltype(std::declval<T>().image),decltype(std::declval<T>().p8),decltype(std::declval<T>().display)>>{
 static void check(const T&t,bool image,bool p8){require(t.version==2&&bool(t.image)==image&&bool(t.p8)==p8&&bool(t.display),"exact typed media/provider/display selection");}
};
template<class T,class=void>struct ViewFields{static void check(const T&){require(false,"typed sealed HUNet view binding required");}};
template<class T>struct ViewFields<T,std::void_t<decltype(std::declval<T>().display->hunet)>>{
 static void check(const T&t){require(t.display->hunet&&t.display->hunet->directory=="/opt/vxgpu/hunet-view"&&t.display->hunet->manifest==digest(18),"exact typed source view identity");}
};
void reject(const std::string&s,uint32_t p,const std::function<void(json_object*)>&change,const char*why){auto o=vxnn::json::parse(s);change(o.get());require(!decode(vxnn::json::dump(o.get()),p),why);}
}
int main(){
 const auto image=manifest(1,true,false),p8=manifest(2,false,true);
 auto a=decode(image,1);require(bool(a),"U20 sealed S8 IMAGE installation must be admitted without inventing NN or P8 support");Fields<NativeInstallation>::check(*a.value,true,false);require(a.value->nn.empty(),"IMAGE cannot fabricate an NN slot");
 auto b=value(decode(p8,2),"exact P8-only installation");Fields<NativeInstallation>::check(b,false,true);require(b.nn.empty(),"P8 never allocates S8 NN models");
 auto both=value(decode(manifest(1,true,false,true),1),"S8 two NN models plus IMAGE");require(both.nn.size()==2,"media does not consume either NN model slot");
 auto exports=vxnn::json::parse(manifest(1,true,false,true));auto*configured=json_object_new_object();json_object_object_add(configured,"build_id",json_object_new_string("DECLARED-HOST-FIXTURE-NOT-IMAGE"));json_object_object_add(exports.get(),"mosaic_export",configured);const auto export_text=vxnn::json::dump(exports.get());auto exported=value(decode(export_text,1),"declared native Mosaic export installs without changing old program contracts");require(exported.mosaic_export&&exported.mosaic_export->build_id=="DECLARED-HOST-FIXTURE-NOT-IMAGE","typed actual declared build binding retained");
 reject(export_text,1,[](json_object*o){json_object_object_add(vxnn::json::member(o,"mosaic_export"),"build_id",json_object_new_string("../untrusted"));},"build identity cannot be an arbitrary path");
 reject(export_text,1,[](json_object*o){auto*n=json_object_new_array();auto m=vxnn::json::parse(model("hunet","hunet"));json_object_array_add(n,json_object_get(m.get()));json_object_object_add(o,"nn",n);},"HUNet alone cannot claim Mosaic export provenance");
 reject(p8,2,[&](json_object*o){json_object_object_add(o,"mosaic_export",json_object_get(configured));},"P8 cannot inherit an S8 Mosaic file producer");
 auto view=vxnn::json::parse(manifest(1,true,false,true));auto*asset=json_object_new_object();json_object_object_add(asset,"directory",json_object_new_string("/opt/vxgpu/hunet-view"));json_object_object_add(asset,"sha256",json_object_new_string(hex(18).c_str()));json_object_object_add(vxnn::json::member(view.get(),"display"),"hunet",asset);auto view_raw=vxnn::json::dump(view.get());auto admitted=decode(view_raw,1);require(bool(admitted),"U20 sealed HUNet source/labels view must be installable on native S8 NN");ViewFields<NativeInstallation>::check(*admitted.value);
 reject(view_raw,1,[](json_object*o){auto*h=vxnn::json::member(vxnn::json::member(o,"display"),"hunet");json_object_object_add(h,"directory",json_object_new_string("../untrusted"));},"view path cannot escape installation");
 reject(view_raw,1,[](json_object*o){auto*h=vxnn::json::member(vxnn::json::member(o,"display"),"hunet");json_object_object_add(h,"sha256",json_object_new_string(std::string(64,'0').c_str()));},"view SHA cannot be zero");
 reject(view_raw,1,[](json_object*o){auto*n=json_object_new_array();auto m=vxnn::json::parse(model("mosaic","mosaic"));json_object_array_add(n,json_object_get(m.get()));json_object_object_add(o,"nn",n);},"HUNet view requires a registered HUNet model");
 reject(p8,2,[&](json_object*o){json_object_object_add(vxnn::json::member(o,"display"),"hunet",json_object_get(asset));},"P8 cannot borrow S8 HUNet assets");
 require(!decode(manifest(1,false,true),1)&&!decode(manifest(2,true,false),2)&&!decode(manifest(2,false,true,true),2),"provider capabilities isolated despite equal numeric program IDs");
 require(!decode(manifest(1,false,false),1),"empty version2 cannot report a ready application set");
 auto old=vxnn::json::parse(manifest(1,false,false,true));json_object_object_add(old.get(),"version",json_object_new_int(1));for(const auto*k:{"image","p8","display"})json_object_object_del(old.get(),k);require(bool(decode(vxnn::json::dump(old.get()),1)),"original version1 NN installation preserved");
 reject(image,1,[](json_object*o){json_object_object_add(o,"raw_program",json_object_new_int(9));},"raw program not configurable by installer");
 reject(image,1,[](json_object*o){json_object_object_del(o,"display");},"media requires explicit display binding");
 reject(image,1,[](json_object*o){json_object_object_add(o,"display",nullptr);},"media cannot silently disable display");
 reject(image,1,[](json_object*o){auto*d=vxnn::json::member(o,"display");json_object_object_add(d,"device",json_object_new_string("/dev/dri/../card0"));},"display traversal refused");
 reject(image,1,[](json_object*o){auto*d=vxnn::json::member(o,"image");json_object_object_add(d,"width",json_object_new_int(256));},"installer cannot replace original native720 geometry");
 for(const auto*key:{"port","session","job_budget_ns"})reject(image,1,[key](json_object*o){json_object_object_add(vxnn::json::member(o,"image"),key,json_object_new_int(0));},"nonzero original endpoint/deadline required");
 reject(image,1,[](json_object*o){json_object_object_add(vxnn::json::member(o,"image"),"port",json_object_new_int(65536));},"UDP port cannot truncate");
 reject(image,1,[](json_object*o){json_object_object_add(vxnn::json::member(o,"image"),"session",json_object_new_uint64(UINT64_C(1)<<32));},"session cannot truncate at native wire boundary");
 reject(image,1,[](json_object*o){json_object_object_add(vxnn::json::member(o,"image"),"job_budget_ns",json_object_new_uint64(UINT64_C(60000000001)));},"original watchdog maximum preserved");
 reject(image,1,[](json_object*o){json_object_object_add(vxnn::json::member(o,"image"),"implementation",json_object_new_string(std::string(64,'0').c_str()));},"zero implementation identity refused");
 reject(p8,2,[](json_object*o){json_object_object_add(vxnn::json::member(o,"p8"),"assets",json_object_new_string(std::string(64,'0').c_str()));},"zero P8 assets identity refused");
 reject(image,1,[](json_object*o){json_object_object_add(vxnn::json::member(o,"image"),"bundle",json_object_new_string(std::string(64,'0').c_str()));},"zero source bundle identity refused");
 reject(image,1,[](json_object*o){json_object_object_add(vxnn::json::member(o,"display"),"device",json_object_new_string("/dev/null"));},"display is a DRM primary node, never arbitrary device");
 reject(image,1,[](json_object*o){json_object_object_add(vxnn::json::member(o,"image"),"address",json_object_new_string("127.0.0.1 extra"));},"normalized IPv4 endpoint required");
 reject(image,1,[](json_object*o){json_object_object_add(vxnn::json::member(o,"image"),"bundle",json_object_new_string("not-a-digest"));},"exact source bundle identity required");
 reject(image,1,[](json_object*o){auto*r=vxnn::json::member(vxnn::json::member(o,"image"),"readonly");json_object_object_add(r,"path",json_object_new_string("/opt/vxgpu/../tables.bin"));},"read-only asset path cannot escape");
 reject(p8,2,[](json_object*o){json_object_object_add(vxnn::json::member(o,"p8"),"display_base",json_object_new_uint64(UINT64_C(1)<<32));},"logical display-cookie namespace bound unchanged");
 auto foreign=hardware(2);foreign.bit[0]^=1;require(!decode_native_installation(p8,foreign,vxnn::content_digest(p8)),"P8 installation binds its actual complete hardware identity");
 require(!decode_native_installation(image,hardware(1),digest(33)),"sealed installer SHA still required");
 auto noepoch=hardware(1);noepoch.device_generation=0;require(!decode_native_installation(image,noepoch,vxnn::content_digest(image)),"startup must carry actual nonzero device epoch");
 puts("U20_INSTALLATION_V2_S8_NN_IMAGE_P8_ISOLATED_TYPED_BINDINGS_AND_V1_COMPAT_PASS_METADATA_ONLY_NOT_NATIVE_EXECUTION_GPU_DP gpu_raw_cycles=null");
}

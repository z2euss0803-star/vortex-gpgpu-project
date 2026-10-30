#include <vxunified/installation.hpp>
#include "nnrt/package/json.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
using namespace vxunified;
namespace j=vxnn::json;
int main(int argc,char**argv){
 if(argc!=2)return 2;
 std::ifstream f(argv[1]);std::ostringstream buf;buf<<f.rdbuf();const auto actual=buf.str();auto tree=j::parse(actual);auto*h=j::member(tree.get(),"hardware");BackendIdentity hw;
 hw.hardware_contract=j::digest(j::member(h,"hardware_contract"));hw.bit=j::digest(j::member(h,"bit"));hw.xsa=j::digest(j::member(h,"xsa"));hw.compatible=j::digest(j::member(h,"compatible"));hw.provider_flags=j::u32(j::member(h,"provider_flags"));hw.device_generation=1;
 auto decode=[&](const std::string&raw){return decode_native_installation(raw,hw,vxnn::content_digest(raw));};
 auto first=decode(actual);if(!first){std::cerr<<"ACTUAL_R10_NINE_ENTRY_REJECTED code="<<first.error->code<<" category="<<first.error->category<<" detail="<<first.error->detail<<'\n';return 86;}
 if(first.value->nn.size()!=9)return 87;
 unsigned checks=1;
 auto expect=[&](bool pass,const char*name){if(!pass){std::cerr<<"FAIL "<<name<<'\n';std::exit(88);}++checks;};
 auto with_count=[&](size_t count,int version){auto t=j::parse(actual);auto*a=j::member(t.get(),"nn");auto*next=json_object_new_array();for(size_t i=0;i<count;++i){auto m=j::parse(j::dump(j::at(a,i%9)));std::string id="candidate-"+std::to_string(i);json_object_object_add(m.get(),"id",json_object_new_string(id.c_str()));json_object_array_add(next,json_object_get(m.get()));}json_object_object_add(t.get(),"nn",next);if(count<4)json_object_object_del(t.get(),"mosaic_export");if(version==1){json_object_object_add(t.get(),"version",json_object_new_int(1));for(auto key:{"display","image","p8","mosaic_export"})json_object_object_del(t.get(),key);}return j::dump(t.get());};
 for(size_t n:{size_t(1),size_t(2),size_t(8),size_t(9)})expect(bool(decode(with_count(n,2))),"version2 count within9");
 expect(!decode(with_count(10,2)),"version2 tenth entry rejected");expect(!decode(with_count(16,2)),"version2 sixteen rejected");
 expect(bool(decode(with_count(2,1))),"legacy version1 two admitted");expect(!decode(with_count(3,1)),"legacy version1 three rejected");
 auto dup=j::parse(actual);auto*arr=j::member(dup.get(),"nn");json_object_object_add(j::at(arr,8),"id",json_object_new_string(j::str(j::member(j::at(arr,0),"id")).c_str()));expect(!decode(j::dump(dup.get())),"duplicate id rejected within9");
 expect(!decode_native_installation(actual,hw,Digest{}),"wrong installation seal rejected");
 auto bad=j::parse(actual);json_object_object_add(j::at(j::member(bad.get(),"nn"),8),"package",json_object_new_string("/a/../escape"));expect(!decode(j::dump(bad.get())),"ninth model path check retained");
 expect(!decode(actual+std::string(65536,' ')),"64KiB installation cap retained");
 std::cout<<"R11_INSTALLATION_ACTUAL_NINE_ENTRY_AND_BOUNDS_PASS checks="<<checks<<" HOST_METADATA_ONLY_NOT_BOARD_GPU_DP\n";
}

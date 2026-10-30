#include <vxunified/texture.hpp>
#include <cstdio>
#include <cstring>
using namespace vxunified;
int main(int argc,char**argv){
 if(argc!=4||std::strlen(argv[2])!=64){return 2;}
 Digest id{};for(unsigned i=0;i<32;i++){unsigned n;if(std::sscanf(argv[2]+2*i,"%2x",&n)!=1)return 2;id[i]=n;}
 auto source=import_result(std::string(argv[1])+"/source/__mosaic_result.json");bool accepted=false;std::string reason;
 if(source){auto result=make_texture(*source.value,{argv[1],id});accepted=bool(result);if(!result)reason=result.error->detail;}else reason=source.error->detail;
 const bool expected=std::string(argv[3])=="valid";if(expected!=accepted){std::fprintf(stderr,"U17_ADMISSION_ASSERT %s reason=%s\n",expected?"valid identity order rejected":"invalid package admitted",reason.c_str());return 1;}
 std::printf("U17_PACKAGE_ADMISSION_EXPECTED_%s gpu_raw_cycles=null\n",expected?"VALID":"REFUSAL");return 0;
}

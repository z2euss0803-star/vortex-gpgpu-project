#include "hunet_tool.h"
#include "hunet_timing.h"
int main(int argc,char **argv){
 if(argc==2&&!strcmp(argv[1],"--help")){puts("NO_DEFAULT_COMPUTE: --release DIR --out NEWDIR --profile hw4-hunet-v5");return 0;}
 try{
  auto o=options(argc,argv);if(o.size()!=3||!o.count("--release")||!o.count("--out")||o["--profile"]!="hw4-hunet-v5")throw std::runtime_error("EXPLICIT_COPY_SCOPE_REQUIRED");
  ArtifactWriter w(o["--out"]);ToolClient c;require(vxl_hunet_client_open(&c.c,"/dev/vortex0"),"open");vxl_hunet_binding_v1 binding{};require(vxl_hunet_read_asset(o["--release"].c_str(),"binding.bin",&binding,256),"binding");if(memcmp(binding.profile_sha256,c.c.binding.profile_sha256,224))throw std::runtime_error("RELEASE_MISMATCH");w.write("binding.bin",&c.c.binding,256);
  const unsigned lengths[]={1,15,16,17,63,64,65,4095,4096,4097,65536,196608,23685056},offsets[]={0,1,15,63};unsigned index=0;std::string cases="[";
  for(unsigned n:lengths)for(unsigned off:offsets){
   uint64_t h=0;size_t size=n+off+128;std::vector<uint8_t> src(size,0xa5),dst(size);require(vxl_hunet_client_reserve(&c.c,VXL_DATA,0,size,&h),"alloc");require(vxl_hunet_client_copy(&c.c,h,0,src.data(),size,1),"initial guards");for(unsigned i=0;i<n;i++)src[64+off+i]=uint8_t(i*37)^0x5b;
   auto begin=hunet_now();require(vxl_hunet_client_copy(&c.c,h,64+off,src.data()+64+off,n,1),"odd write");require(vxl_hunet_client_copy(&c.c,h,0,dst.data(),size,0),"readback");auto duration=hunet_duration(begin,hunet_now());if(src!=dst)throw std::runtime_error("COPY_GUARD_OR_DATA_MISMATCH");require(vxl_hunet_client_free(&c.c,h),"free");
   auto prefix="case"+std::to_string(index++);w.write(prefix+"-input.bin",src.data(),src.size());w.write(prefix+"-output.bin",dst.data(),dst.size());if(cases.size()>1)cases+=",";cases+="{\"length\":"+std::to_string(n)+",\"offset\":"+std::to_string(off)+",\"ns\":"+std::to_string(duration)+"}";
  }
  w.write("copy.json","{\"scope\":\"HW4_LINUX_CDMA_CAPTURE\",\"cases\":"+cases+"],\"files\":"+w.manifest()+"}\n");return 0;
 }catch(const std::exception &e){fprintf(stderr,"COPY_FAIL_STOP: %s\n",e.what());return 1;}
}

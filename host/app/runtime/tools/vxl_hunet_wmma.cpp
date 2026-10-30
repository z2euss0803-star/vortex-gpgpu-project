#include "hunet_tool.h"
#include "hunet_timing.h"
int main(int argc,char **argv){
 if(argc==2&&!strcmp(argv[1],"--help")){puts("NO_DEFAULT_COMPUTE: --release DIR --case 0|1|both --out NEWDIR");return 0;}
 try{
  auto o=options(argc,argv);if(o.size()!=3||!o.count("--release")||!o.count("--out")||(o["--case"]!="0"&&o["--case"]!="1"&&o["--case"]!="both"))throw std::runtime_error("EXPLICIT_WMMA_SCOPE_REQUIRED");
  ArtifactWriter w(o["--out"]);ToolClient c;require(vxl_hunet_client_open(&c.c,"/dev/vortex0"),"open");require(vxl_hunet_client_load_wmma(&c.c,o["--release"].c_str()),"load");w.write("binding.bin",&c.c.binding,256);
  for(unsigned index=0;index<2;index++){
   if(o["--case"]!="both"&&number(o["--case"])!=index)continue;
   unsigned m=index?128:8;std::string prefix="wmma/case"+std::to_string(index)+"/";std::vector<uint8_t> mem(16384,0xa5),expected(m*4*4),result(expected.size()),trace((m/8)*4);vxl_hunet_job_v1 j{};
   require(vxl_hunet_read_asset(o["--release"].c_str(),(prefix+"args.bin").c_str(),&j,128),"args");require(vxl_hunet_read_asset(o["--release"].c_str(),(prefix+"matrix.bin").c_str(),mem.data()+0x40,32),"matrix");require(vxl_hunet_read_asset(o["--release"].c_str(),(prefix+"a.f16").c_str(),mem.data()+0x100,m*8*2),"A");require(vxl_hunet_read_asset(o["--release"].c_str(),(prefix+"b.f16").c_str(),mem.data()+0x2000,64),"B");require(vxl_hunet_read_asset(o["--release"].c_str(),(prefix+"expected.f32").c_str(),expected.data(),expected.size()),"reference");require(vxl_hunet_client_copy(&c.c,c.c.handles[2],0,mem.data(),mem.size(),1),"fixture load");
   uint64_t id=0,start=hunet_now();vxl_hunet_job_evidence_v1 e{};require(vxl_hunet_client_submit(&c.c,&j,&id),"submit7");require(vxl_hunet_client_wait(&c.c,id,60000),"wait");auto ns=hunet_duration(start,hunet_now());require(vxl_hunet_client_evidence(&c.c,id,&e),"evidence");require(vxl_hunet_client_copy(&c.c,c.c.handles[2],0x2100,result.data(),result.size(),0),"GPU result");require(vxl_hunet_client_copy(&c.c,c.c.handles[2],0x3c00,trace.data(),trace.size(),0),"owner trace");
   auto out="case"+std::to_string(index);w.write(out+"-args.bin",&j,128);w.write(out+"-evidence.bin",&e,256);w.write(out+"-result.f32",result.data(),result.size());w.write(out+"-reference.f32",expected.data(),expected.size());w.write(out+"-trace.u32",trace.data(),trace.size());w.write(out+"-time.json","{\"submit_wait_ns\":"+std::to_string(ns)+"}\n");
   if(result!=expected||e.program_id!=7||(e.valid_flags&7)!=7)throw std::runtime_error("WMMA_EXACT_OR_OWNER_MISMATCH");
   require(vxl_hunet_client_reap(&c.c,id),"reap");
  }
  w.write("wmma.json","{\"scope\":\"HW4_LINUX_ORIGINAL_WMMA_CAPTURE\",\"case\":"+quote(o["--case"])+",\"files\":"+w.manifest()+"}\n");return 0;
 }catch(const std::exception &e){fprintf(stderr,"WMMA_FAIL_STOP: %s\n",e.what());return 1;}
}

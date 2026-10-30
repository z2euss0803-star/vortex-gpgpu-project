#include <vxunified/mosaic_core_probe.hpp>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
using namespace vxunified;
static void ck(bool x,const char*s){if(!x){std::fprintf(stderr,"CORE_PROBE_ASSERT %s\n",s);std::exit(71);}}
int main(int argc,char**argv){
 ck(argc==2,"private output argument");std::string root=argv[1];ck(!mkdir(root.c_str(),0700),"fresh0700");BufferRef work{9,7,128,34880,3,2};unsigned calls=0;
 auto read=[&](BufferRef ref,vxnn::MutableBytes b)->Result<void>{unsigned c=calls++;ck(c<4&&ref.handle==9&&ref.object_generation==7&&ref.device_generation==2,"same owner identity");ck(ref.offset==128+64+c*8704+8192&&ref.bytes==512&&ref.access==1&&b.size==512,"four exact distinct accumulator slices");for(size_t i=0;i<b.size;i++)b.data[i]=uint8_t(c*53+i);return Result<void>::success();};
 ck(bool(mosaic_core_probe_capture(root,4,false,work,256,8704,read)),"before captured");ck(calls==4,"four distinct reads");calls=0;ck(!mosaic_core_probe_capture(root,4,false,work,256,8704,read)&&calls==0,"cannot overwrite or silently recapture");
 std::ifstream in(root+"/core-accum-index4-before.bin",std::ios::binary);std::string raw((std::istreambuf_iterator<char>(in)),{});ck(raw.size()==2048,"exact raw proof size");for(unsigned c=0;c<4;c++)for(size_t i=0;i<512;i++)ck(uint8_t(raw[c*512+i])==uint8_t(c*53+i),"raw values unchanged");struct stat st{};ck(!lstat((root+"/core-accum-index4-before.bin").c_str(),&st)&&((st.st_mode&0777)==0400),"immutable snapshot mode");
 calls=0;auto short_work=work;short_work.bytes=34879;ck(!mosaic_core_probe_capture(root,5,false,short_work,256,8704,read)&&calls==0,"whole four-core extent checked before I/O");
 calls=0;auto fault=[&](BufferRef,vxnn::MutableBytes)->Result<void>{++calls;return Result<void>::failure({EIO,"TEST","readback failure"});};ck(!mosaic_core_probe_capture(root,6,true,work,256,8704,fault)&&calls==1,"readback error propagates without fabricated data");
 ck(!symlink(root.c_str(),(root+"-link").c_str()),"symlink fixture");calls=0;ck(!mosaic_core_probe_capture(root+"-link",7,false,work,256,8704,read)&&calls==0,"directory symlink refused");
 puts("MOSAIC_CORE_READONLY_SNAPSHOT_BOUNDS_OWNER_RAW_BYTES_NO_OVERWRITE_ERROR_PROPAGATION_HOST_PASS_NOT_GPU");return 0;
}

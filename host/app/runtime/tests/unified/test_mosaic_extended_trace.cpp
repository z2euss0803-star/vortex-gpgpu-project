#include <vxunified/mosaic_request.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
using namespace vxunified;
static void ck(bool v,const char*s){if(!v){fprintf(stderr,"EXT_TRACE_ASSERT %s\n",s);exit(78);}}
static uint32_t word(const std::vector<uint8_t>&b,size_t offset){return uint32_t(b[offset])|uint32_t(b[offset+1])<<8|uint32_t(b[offset+2])<<16|uint32_t(b[offset+3])<<24;}
int main(int argc,char**argv){
 ck(argc==2,"output fixture path");MosaicJobTrace t;t.index=7;t.node=2;t.first=1;t.count=2;t.begin_ns=10;t.end_ns=20;t.scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;
 auto old=encode_mosaic_extended_trace(t);ck(bool(old)&&old.value->size()==512&&word(*old.value,4)==1,"unmeasured legacy record remains512/version1");
 t.linux_detail=vxl_v2_completion{};t.linux_detail->cookie=19;t.profile_prepare_ns=2;t.profile_read_ns=3;
 VxnnMosaicArgs a{};a.abi=3;a.bytes=128;a.opcode=2;a.first=1;a.count=2;a.ci=3;a.co=8;a.h=8;a.w=8;a.k=3;a.kpad=32;a.stride=a.upsample=1;a.reserved=1;t.stage_args=a;
 auto partial=encode_mosaic_extended_trace(t);ck(!partial,"partial pair rejected");t.stage_records=MosaicProfileRecords{};
 for(unsigned c=0;c<4;c++){auto&h=(*t.stage_records)[c].header;h={MOSAIC_PROFILE_MAGIC_V1,2,256,1,2,c,1,2,1,1,1,1,MOSAIC_PROFILE_COMPLETE_V1,0,0,mosaic_profile_argument_tag(a)};}
 auto encoded=encode_mosaic_extended_trace(t);ck(bool(encoded)&&encoded.value->size()==1536&&word(*encoded.value,4)==2&&word(*encoded.value,8)==1536,"measured record self describes length1536/version2");ck(!memcmp(encoded.value->data()+128,&a,128),"exact unrelocated args");ck(!memcmp(encoded.value->data()+256,&*t.linux_detail,256),"original Linux completion unchanged");ck(!memcmp(encoded.value->data()+512,t.stage_records->data(),1024),"all4 raw counters retained");
 std::ofstream file(argv[1],std::ios::binary);file.write(reinterpret_cast<const char*>(old.value->data()),old.value->size());file.write(reinterpret_cast<const char*>(encoded.value->data()),encoded.value->size());ck(bool(file),"mixed old/new fixture saved");file.close();
 t.error=EIO;ck(!encode_mosaic_extended_trace(t),"failed job cannot become complete stage result");t.error=0;t.stage_args->count=3;ck(!encode_mosaic_extended_trace(t),"range differs");t.stage_args->count=2;t.linux_detail.reset();ck(!encode_mosaic_extended_trace(t),"completion required");puts("MOSAIC_TRACE_LEGACY512_EXTENDED1536_ARGS_LINUX_SNAPSHOT_RAW4_HOST_PASS_NOT_BOARD");return 0;
}

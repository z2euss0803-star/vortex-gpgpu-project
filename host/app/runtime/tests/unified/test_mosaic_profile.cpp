#include <vxunified/mosaic_profile.hpp>
#include "device/vxnn/mosaic_wire_reuse_v4.h"
#define main validator_regression_main
#include "tests/nnrt/test_mosaic_coop_v3.cpp"
#undef main
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace vxunified;
static unsigned assertions;
static void ck(bool ok,const char*why){++assertions;if(!ok){fprintf(stderr,"PROFILE_ASSERT %s\n",why);exit(78);}}
int main(){
 for(unsigned abi:{3u,4u})for(unsigned op:{1u,2u,3u,7u}){
  auto a=args(op);a.abi=abi;a.reserved=1;if(abi==4&&op==2)a.extent[VXM_WEIGHTS]=((a.co+7)/8)*a.kpad*16;VxnnMosaicShape shape{};ck(!(abi==4?vxnn_mosaic_validate_reuse_v4(&a,false,&shape):vxnn_mosaic_validate_coop_v3(&a,false,&shape)),"valid underlying wire");MosaicWorkLayoutV1 l{};ck(!(abi==4?mosaic_reuse_work_layout_v4(op,a.kpad,shape.norm_elements,&l):mosaic_work_layout_v1(op,a.kpad,shape.norm_elements,&l)),"layout");
  std::array<MosaicProfileRecordV1,4>records{};
  for(unsigned c=0;c<4;c++){
   mosaic_profile_initialize(&records[c],a,l,c);auto&h=records[c].header;h.status=MOSAIC_PROFILE_COMPLETE_V1;
   if(c==0){if(op==2){for(unsigned j=0;j<5;j++){records[c].stage[j]={55,1,2,1};h.valid_stages|=1u<<j;}records[c].stage[2].units=27;records[c].stage[3].units=24;}else{records[c].stage[5]={55,1,4,1};h.valid_stages=1u<<5;}}
  }
  BufferRef work{9,7,128,l.total_work_bytes,3,2};unsigned reads=0;
  auto read=[&](BufferRef ref,vxnn::MutableBytes bytes)->Result<void>{unsigned c=reads++;ck(c<4&&ref.handle==9&&ref.object_generation==7&&ref.device_generation==2,"owner/epoch unchanged");ck(ref.offset==128+64+uint64_t(c)*l.core_stride+l.profile_offset&&ref.bytes==256&&ref.access==1&&bytes.size==256,"exact4 bounded profile slices");memcpy(bytes.data,&records[c],256);return Result<void>::success();};
  auto good=mosaic_profile_read(a,work,read);ck(bool(good)&&reads==4&&!memcmp(good.value->data(),records.data(),1024),"raw records preserved");
  {auto saved=records[0];records[0].stage[op==2?2:5].cycles=0;reads=0;auto unknown=mosaic_profile_read(a,work,read);ck(!unknown&&unknown.error->code==ENODATA,"zero CSR with work is unavailable, never zero elapsed");records[0]=saved;}
  for(unsigned field=0;field<16;field++){
   auto saved=records[0];auto*words=reinterpret_cast<uint32_t*>(&records[0].header);words[field]^=0x80000000u;reads=0;ck(!mosaic_profile_read(a,work,read),"all header mismatches refused");records[0]=saved;
  }
  {auto saved=records[2];records[2].stage[0].calls=1;reads=0;ck(!mosaic_profile_read(a,work,read),"idle core cannot report work");records[2]=saved;}
  {auto bad=a;bad.h+=2;reads=0;ck(!mosaic_profile_read(bad,work,read),"different geometry rejected");}
  {auto bad=work;--bad.bytes;reads=0;ck(!mosaic_profile_read(a,bad,read)&&!reads,"short work before read");}
  {auto bad=a;bad.reserved=0;reads=0;ck(!mosaic_profile_read(bad,work,read)&&!reads,"unmeasured profile cannot become zero time");}
  reads=0;auto failed=[&](BufferRef,vxnn::MutableBytes)->Result<void>{++reads;return Result<void>::failure({EIO,"TEST","read fault"});};ck(!mosaic_profile_read(a,work,failed)&&reads==1,"first I/O error no retry");
  auto tag=mosaic_profile_argument_tag(a);auto relocated=a;for(unsigned c=0;c<8;c++)relocated.address[c]=0x90000000+c*64;ck(mosaic_profile_argument_tag(relocated)==tag,"only relocation is omitted from metadata tag");auto geometry=a;++geometry.co;ck(mosaic_profile_argument_tag(geometry)!=tag,"geometry participates in metadata tag");
 }
 printf("MOSAIC_PROFILE_%u_BOUNDS_METADATA_IDLE_ZERO_TIMER_IO_HOST_PASS_NOT_CSR_OR_BOARD\n",assertions);return 0;
}

#include "device/vxnn/mosaic_work_layout_v1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x,m) do{++checks;if(!(x)){fprintf(stderr,"WORK_LAYOUT_ASSERT %s check=%u\n",m,checks);exit(72);}}while(0)
static int apart(uint32_t a,uint32_t an,uint32_t b,uint32_t bn){return (uint64_t)a+an<=b||(uint64_t)b+bn<=a;}
static void regions(const MosaicWorkLayoutV1 *l){
 uint32_t offsets[5]={l->a_offset,l->b_offset,l->acc_offset,l->raw_offset,l->profile_offset};
 uint32_t sizes[5]={l->a_bytes,l->b_bytes,l->acc_bytes,l->raw_bytes,l->profile_bytes};
 for(unsigned c=0;c<4;c++)for(unsigned i=0;i<5;i++)if(sizes[i]){
  uint64_t base=64+(uint64_t)c*l->core_stride;
  CHECK(offsets[i]%64==0,"all region starts aligned64");CHECK((uint64_t)offsets[i]+sizes[i]<=l->core_stride,"region bounded by owned core");
  CHECK(base+offsets[i]+sizes[i]<=l->total_work_bytes,"region bounded by full WORK");
  for(unsigned d=0;d<4;d++)for(unsigned j=0;j<5;j++)if(sizes[j]&&(c!=d||i!=j))CHECK(apart((uint32_t)(base+offsets[i]),sizes[i],64+d*l->core_stride+offsets[j],sizes[j]),"all cores and all active regions disjoint");
 }
}
int main(void){
 MosaicWorkLayoutV1 l;
 CHECK(!mosaic_work_layout_v1(2,32,0,&l),"minimum convolution accepted");
 CHECK(l.a_offset==0&&l.a_bytes==512&&l.b_offset==512&&l.b_bytes==512&&l.acc_offset==1024&&l.acc_bytes==512,"literal minimum A/B/ACC offsets");
 CHECK(l.raw_offset==1536&&l.raw_bytes==384&&l.profile_offset==1920&&l.profile_bytes==256&&l.core_stride==2176&&l.total_work_bytes==8768,"literal min raw/profile/stride/total");regions(&l);
 unsigned char memory[8768+128];memset(memory,0xa5,sizeof memory);
 for(unsigned c=0;c<4;c++){
  unsigned char *base=memory+64+64+c*l.core_stride;
  memset(base+l.acc_offset,0x31,512);memset(base+l.raw_offset,0x42,384);memset(base+l.profile_offset,0x53,256);
  CHECK(base[l.acc_offset+511]==0x31&&base[l.raw_offset]==0x42&&base[l.raw_offset+383]==0x42&&base[l.profile_offset]==0x53&&base[l.profile_offset+255]==0x53,"real boundary stores preserve ACC/raw/profile separation");
 }
 for(unsigned i=0;i<128;i++)CHECK(memory[i]==0xa5,"outer head and canonical prefix unchanged");
 for(unsigned i=8832;i<sizeof memory;i++)CHECK(memory[i]==0xa5,"outer tail unchanged");
 CHECK(!mosaic_work_layout_v1(2,2592,0,&l)&&l.core_stride==84096&&l.total_work_bytes==336448&&l.raw_offset==83456&&l.profile_offset==83840,"literal max convolution layout");regions(&l);
 CHECK(!mosaic_work_layout_v1(3,32,1,&l)&&l.compute_bytes==64&&l.core_stride==320&&l.profile_offset==64&&l.total_work_bytes==1344,"literal min norm aligned tree");regions(&l);
 CHECK(!mosaic_work_layout_v1(3,1152,2097152,&l)&&l.compute_bytes==16777216&&l.core_stride==16777472&&l.total_work_bytes==67109952,"literal max norm layout");regions(&l);
 for(unsigned op=1;op<=8;op++)if(op!=2&&op!=3){CHECK(!mosaic_work_layout_v1(op,32,0,&l)&&l.compute_bytes==0&&l.core_stride==256&&l.total_work_bytes==1088&&l.profile_offset==0&&l.raw_bytes==0&&l.raw_offset==UINT32_MAX,"all point operations get four owned profile records");regions(&l);}
 CHECK(l.schedule_version==MOSAIC_SCHEDULE_CHANNEL_MAJOR_GLOBAL_MOD4_V1&&l.weight_layout_version==MOSAIC_WEIGHT_CO_KPAD_I16_V1,"schedule and original weight layout unchanged");
 MosaicProfileRecordV1 record;memset(&record,0,sizeof record);record.header.timer_kind=MOSAIC_TIMER_RV32_CSR64_V1;record.stage[5].cycles=UINT64_C(0x1234567887654321);record.stage[5].units=UINT64_MAX;
 CHECK(sizeof record==256&&record.stage[5].cycles==UINT64_C(0x1234567887654321)&&record.stage[5].units==UINT64_MAX,"C wire retains full64-bit counters");
 MosaicWorkLayoutV1 before=l;
 for(unsigned k=0;k<2625;k++)if(k<32||k%32||k>2592)CHECK(mosaic_work_layout_v1(2,k,0,&l)<0&&!memcmp(&l,&before,sizeof l),"bad kpad rejected without publishing layout");
 CHECK(mosaic_work_layout_v1(3,32,0,&l)<0&&mosaic_work_layout_v1(3,32,3,&l)<0&&mosaic_work_layout_v1(3,32,4194304,&l)<0,"invalid norm tree rejected");
 CHECK(mosaic_work_layout_v1(0,32,0,&l)<0&&mosaic_work_layout_v1(9,32,0,&l)<0&&mosaic_work_layout_v1(1,32,1,&l)<0&&mosaic_work_layout_v1(2,32,1,&l)<0&&mosaic_work_layout_v1(1,32,0,NULL)<0,"bad opcode/non-norm tree/null rejected");
 printf("MOSAIC_WORK_LAYOUT_PROFILE256_C_RAW384_ACC512_FOUR_CORE_BOUNDARIES_HOST_PASS checks=%u\n",checks);return 0;
}

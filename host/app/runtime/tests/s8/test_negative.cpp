/* Instruction-boundary CPU fixture only; not an RTL/S8 implementation test. */
#include "../../device/s8/negative.h"
#include "../../tools/s8/reference.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cerrno>
struct Model {bool active=false;unsigned owner=0,k=0,accepted=0,pages=0,mask=15,begins1=0,fault=0;uint32_t error[4]={0},seen[10]={0};int64_t acc[64]={0};};
struct Port {Model *m;unsigned warp;};
static s8_words4 observation(Model &m,unsigned w,uint32_t code){return m.active&&m.owner==w?s8_words4{{code,m.accepted,m.k,1u|(m.accepted==m.k?2u:0u)|(m.pages==0xffffff?4u:0u)}}:s8_words4{{code,0,0,0}};}
static s8_words4 begin(void *p,const s8_words4 &c){auto &port=*static_cast<Port *>(p);auto &m=*port.m;unsigned e=m.mask!=15?9:(c.word[1]||c.word[2]||c.word[3]||(c.word[0]>>13))?8:(!c.word[0]||c.word[0]>4096)?2:m.active?1:0;
 if(e){m.error[port.warp]=e;m.seen[e]++;return observation(m,port.warp,e);}m.active=true;m.owner=port.warp;m.k=c.word[0];m.accepted=m.pages=0;memset(m.acc,0,sizeof(m.acc));if(!m.fault)m.error[port.warp]=0;if(m.k==1)m.begins1++;return {{0,0,m.k,1}};}
static int step(void *p,const s8_words4 &a,const s8_words4 &b){auto &port=*static_cast<Port *>(p);auto &m=*port.m;unsigned e=(!m.active||m.owner!=port.warp)?3:m.accepted>=m.k?6:0;if(e){m.error[port.warp]=e;m.seen[e]++;return 0;}
 int16_t aa[8],bb[8];for(unsigned lane=0;lane<4;++lane){aa[lane*2]=int16_t(a.word[lane]);aa[lane*2+1]=int16_t(a.word[lane]>>16);bb[lane*2]=int16_t(b.word[lane]);bb[lane*2+1]=int16_t(b.word[lane]>>16);}for(unsigned i=0;i<8;++i)for(unsigned j=0;j<8;++j)m.acc[i*8+j]+=int64_t(aa[i])*bb[j];m.accepted++;return 0;}
static s8_words4 read(void *p,const s8_words4 &c){auto &port=*static_cast<Port *>(p);auto &m=*port.m;unsigned page=c.word[0]&31;bool release=(c.word[0]&256)&&page==31;unsigned e=(c.word[1]||c.word[2]||c.word[3]||(c.word[0]&0xfffffee0u)||((c.word[0]&256)&&page!=31))?8:(page>=24&&page!=31)?5:release?(!m.active||m.owner!=port.warp)?3:(m.accepted!=m.k||m.pages!=0xffffff)?7:0:page<24?(!m.active||m.owner!=port.warp)?3:m.accepted!=m.k?4:0:0;
 if(e){m.error[port.warp]=e;m.seen[e]++;return page==31?observation(m,port.warp,e):s8_words4{{0,0,0,0}};}
 if(page==31){auto v=observation(m,port.warp,release?0:m.error[port.warp]);if(release)m.active=false;return v;}
 uint8_t raw[384];for(unsigned i=0;i<64;++i)for(unsigned byte=0;byte<6;++byte)raw[i*6+byte]=uint8_t(uint64_t(m.acc[i])>>(byte*8));s8_words4 v;memcpy(v.word,raw+page*16,16);m.pages|=1u<<page;return v;}
static void fence(void *){}
static s8_words4 partial(void *p,const s8_words4 &c,volatile vxl_s8_status_v1 *){auto &m=*static_cast<Port *>(p)->m;m.mask=7;auto v=begin(p,c);m.mask=15;v.word[3]=0xdeadbeef;return v;}
static int peer(void *p,unsigned phase,s8_protocol_state *s,const int16_t *a,const int16_t *b){auto &port=*static_cast<Port *>(p);Port other{port.m,(port.warp+1)%4};s8_protocol_io io={&other,begin,step,read,fence,4};s8_protocol_state copy=*s;copy.io=&io;int rc=s8_execute_peer_phase(&copy,phase,a,b);if(!rc){s->meta.status_count=copy.meta.status_count;if(phase==2){s->meta.recovery_elements=64;s->meta.valid_bits|=S8_RECOVERY_RAW_VALID|S8_RECOVERY_DECODED_VALID;}}return rc;}
static int timeout_peer(void *,unsigned,s8_protocol_state *,const int16_t *,const int16_t *){return -ETIMEDOUT;}
static vxl_s8_job_v1 job(unsigned family,unsigned variant){vxl_s8_job_v1 j={};j.magic=VXL_S8_JOB_MAGIC;j.version=1;j.bytes=128;j.case_id=(family<<8)|variant;j.k=2;j.core_mask=1;j.spin_limit=65536;j.generation=2;j.sequence=1;j.run_id[0]=1;j.binding_tag[0]=2;return j;}
int main(){
 int16_t a[16],b[16];for(unsigned t=0;t<2;++t)for(unsigned i=0;i<8;++i){a[t*8+i]=int16_t(i+1);b[t*8+i]=int16_t((t+1)*(i+1));}unsigned total=0;
 const unsigned count[]={2,1,7,2,1,1,1,2,1},codes[]={2,4,5,7,6,3,1,8,9};
 for(unsigned family=201;family<=209;++family)for(unsigned variant=0;variant<count[family-201];++variant){
  Model m;Port port{&m,0};s8_negative_io io={{&port,begin,step,read,fence,4},peer,partial};auto j=job(family,variant);alignas(16) uint8_t out[4096];memset(out,0xcc,4096);memset(out+0x1c0,0,512);
  assert(!s8_execute_negative(&j,0,0,a,32,b,32,out,4096,&io));auto *meta=reinterpret_cast<vxl_s8_result_v1 *>(out+0x3c0);
  assert(meta->done==VXL_S8_DONE&&!meta->sw_status&&meta->status_count<=16&&meta->status_count==s8_negative_status_count(j.case_id));assert(m.seen[codes[family-201]]&&m.begins1==1&&!m.active);assert(meta->recovery_elements==64);
  int64_t expected[64];assert(!s8_reference(a,b,1,expected));assert(!memcmp(out+0xa80,expected,512));
  bool primary=family!=201&&family!=208&&family!=209;
  if(primary){assert(meta->primary_accepted==2&&meta->primary_bitmap==0xffffff&&meta->pages_count==24);assert(!s8_reference(a,b,2,expected));assert(!memcmp(out+0x4c0,expected,512));}
  else {assert(!meta->primary_accepted&&!meta->primary_bitmap&&!meta->primary_elements&&!meta->pages_count);for(unsigned i=0;i<384;++i)assert(out[64+i]==0xcc);}
  if(family==209){auto *row=reinterpret_cast<vxl_s8_status_v1 *>(out+0x1c0);assert(row->valid_mask==7&&row->lane[3]==0);}
  char name[96];snprintf(name,sizeof(name),"build/s8/negative-%u.bin",j.case_id);FILE *saved=fopen(name,"wb");assert(saved);assert(fwrite(out,1,4096,saved)==4096);assert(!fclose(saved));
  total++;
 }
 assert(total==18);
 {Model m;m.fault=1;Port p{&m,0};s8_negative_io io={{&p,begin,step,read,fence,4},peer,partial};auto j=job(201,0);alignas(16) uint8_t out[4096]={0};assert(s8_execute_negative(&j,0,0,a,32,b,32,out,4096,&io)<0);}
 {Model m;Port p{&m,0};s8_negative_io io={{&p,begin,step,read,fence,4},timeout_peer,partial};auto j=job(206,0);alignas(16) uint8_t out[4096]={0};assert(s8_execute_negative(&j,0,0,a,32,b,32,out,4096,&io)==-ETIMEDOUT);assert(m.active&&m.begins1==0);assert(reinterpret_cast<vxl_s8_result_v1 *>(out+0x3c0)->sw_status);}
 puts("S8_ALL18_NEGATIVE_STICKY_OWNER_HANDOFF_RECOVERY_BANKS_PASS INSTRUCTION_BOUNDARY_ONLY");
}

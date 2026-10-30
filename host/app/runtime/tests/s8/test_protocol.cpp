#include "../../device/s8/protocol.h"
#include "../../tools/s8/reference.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <vector>
struct ISA {
 uint32_t k=3,accepted=0,bitmap=0,steps=0,reads=0,polls=0,duplicate=0,fault=0,phase_calls=0;
 bool active=false,raw_fenced=false;uint8_t *output=nullptr;uint32_t raw[96]={0};std::vector<int16_t> a=std::vector<int16_t>(24),b=std::vector<int16_t>(24);
};
static s8_words4 begin(void *p,const s8_words4 &control){auto &s=*static_cast<ISA *>(p);assert(control.word[0]==s.k&&!control.word[1]&&!control.word[2]&&!control.word[3]);s.active=true;return {{0,0,s.k,1}};}
static int step(void *p,const s8_words4 &a,const s8_words4 &b){auto &s=*static_cast<ISA *>(p);for(unsigned lane=0;lane<4;++lane){uint32_t aa=uint16_t(s.a[s.accepted*8+lane*2])|(uint32_t(uint16_t(s.a[s.accepted*8+lane*2+1]))<<16);uint32_t bb=uint16_t(s.b[s.accepted*8+lane*2])|(uint32_t(uint16_t(s.b[s.accepted*8+lane*2+1]))<<16);assert(a.word[lane]==aa&&b.word[lane]==bb);}s.accepted++;s.steps++;return 0;}
static s8_words4 read(void *p,const s8_words4 &control){auto &s=*static_cast<ISA *>(p);assert(!control.word[1]&&!control.word[2]&&!control.word[3]);uint32_t c=control.word[0];
 if(c==31){s.polls++;if(!s.active)return {{0,0,0,0}};if(s.fault==3||s.polls==1)return {{4,s.accepted,s.k,1}};if(s.fault==2)return {{0,s.k,7,3}};return {{0,s.accepted,s.k,s.bitmap==0xffffff?7u:3u}};}
 if(c==0x11f){assert(s.bitmap==0xffffff);s.active=false;return {{0,s.accepted,s.k,7}};}
 assert(c<24);s.reads++;if(s.bitmap&(1u<<c))s.duplicate++;s.bitmap|=1u<<c;s8_words4 result;for(unsigned lane=0;lane<4;++lane)result.word[lane]=s.raw[c*4+lane];if(s.fault==1&&s.duplicate&&c==0)result.word[3]^=1;return result;
}
static void fence(void *p){auto &s=*static_cast<ISA *>(p);if(s.output&&s.bitmap==0xffffff&&s.active&&!s.raw_fenced){assert(!memcmp(s.output+64,s.raw,384));for(unsigned i=0;i<512;++i)assert(s.output[0x4c0+i]==0xcc);s.raw_fenced=true;}}
static int phase(void *p,uint32_t event){auto &s=*static_cast<ISA *>(p);assert(event==s.phase_calls+1);s.phase_calls++;return 0;}
static vxl_s8_job_v1 job(){vxl_s8_job_v1 j={};j.magic=VXL_S8_JOB_MAGIC;j.version=1;j.bytes=128;j.case_id=103;j.k=3;j.core_mask=1;j.owner_warp=0;j.generation=2;j.sequence=1;j.spin_limit=65536;j.run_id[0]=1;j.binding_tag[0]=2;return j;}
static void init(ISA &s){s.a.resize(s.k*8);s.b.resize(s.k*8);for(unsigned t=0;t<s.k;++t)for(unsigned i=0;i<8;++i){s.a[t*8+i]=s.k==4096?-32768:int16_t(i+1);s.b[t*8+i]=s.k==4096?-32768:int16_t((t+1)*(i+1));}int64_t expected[64];assert(!s8_reference(s.a.data(),s.b.data(),s.k,expected));uint8_t raw[384];for(unsigned i=0;i<64;++i)for(unsigned b=0;b<6;++b)raw[i*6+b]=uint8_t(uint64_t(expected[i])>>(b*8));memcpy(s.raw,raw,384);}
int main(){
 ISA s;init(s);alignas(16) uint8_t out[4096];s.output=out;memset(out,0xcc,sizeof(out));s8_protocol_io io={&s,begin,step,read,fence,4,phase};auto j=job();
 assert(!s8_execute_positive(&j,0,0,s.a.data(),48,s.b.data(),48,out,sizeof(out),&io));
 auto *m=reinterpret_cast<vxl_s8_result_v1 *>(out+0x3c0);assert(m->done==VXL_S8_DONE&&m->pages_count==24&&m->primary_accepted==3&&m->primary_bitmap==0xffffff&&m->primary_elements==64);
 assert(s.steps==3&&s.reads==25&&s.duplicate==1&&!s.active&&s.raw_fenced&&!s.phase_calls);assert(!memcmp(out+64,s.raw,384));
 int64_t expected[64];assert(!s8_reference(s.a.data(),s.b.data(),3,expected));assert(!memcmp(out+0x4c0,expected,512));
 for(unsigned fault=1;fault<=3;++fault){ISA bad;init(bad);bad.fault=fault;io.context=&bad;memset(out,0xcc,sizeof(out));assert(s8_execute_positive(&j,0,0,bad.a.data(),48,bad.b.data(),48,out,sizeof(out),&io)<0);assert(reinterpret_cast<vxl_s8_result_v1 *>(out+0x3c0)->sw_status>0&&reinterpret_cast<vxl_s8_result_v1 *>(out+0x3c0)->sw_status<=7);}
 ISA bad;init(bad);io.context=&bad;assert(s8_execute_positive(&j,0,0,bad.a.data(),47,bad.b.data(),48,out,sizeof(out),&io)<0 && !bad.steps);
 assert(s8_execute_positive(&j,0,0,bad.a.data(),48,bad.b.data(),48,out,4095,&io)<0 && !bad.steps);
 memset(out,0xcc,sizeof(out));assert(!s8_execute_positive(&j,0,1,bad.a.data(),48,bad.b.data(),48,out,sizeof(out),&io));for(auto byte:out)assert(byte==0xcc);
 ISA maximum;maximum.k=4096;init(maximum);io.context=&maximum;j.case_id=113;j.k=4096;memset(out,0xcc,sizeof(out));
 assert(!s8_execute_positive(&j,0,0,maximum.a.data(),65536,maximum.b.data(),65536,out,sizeof(out),&io));assert(maximum.steps==4096);
 for(unsigned i=0;i<64;++i)assert(reinterpret_cast<int64_t *>(out+0x4c0)[i]==(INT64_C(1)<<42));
 ISA timed;init(timed);io.context=&timed;j=job();j.flags=1;memset(out,0xcc,sizeof(out));assert(!s8_execute_positive(&j,0,0,timed.a.data(),48,timed.b.data(),48,out,sizeof(out),&io));assert(timed.phase_calls==10);assert(reinterpret_cast<vxl_s8_result_v1 *>(out+0x3c0)->phase_count==12);
 puts("S8_POSITIVE_BEGIN_PACK_STEPS_24PAGES_DUPLICATE_RELEASE_FAILURES_PASS INSTRUCTION_BOUNDARY_ONLY");
}

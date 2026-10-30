#include "protocol.h"
#include "unpack.h"
#include <errno.h>
bool s8_same(const s8_words4 &a,const s8_words4 &b){for(unsigned i=0;i<4;++i)if(a.word[i]!=b.word[i])return false;return true;}
static bool leader(const s8_protocol_state *s){return s->io->participant==0 || s->io->participant==4;}
int s8_state_init(s8_protocol_state *s,const vxl_s8_job_v1 *j,uint32_t core,uint32_t warp,uint8_t *out,uint32_t bytes,const s8_protocol_io *io){
 if(!s||!j||!out||bytes!=4096||!io||!io->begin||!io->step||!io->read||!io->fence||io->participant>4||core>3||warp>3||j->owner_warp>3||j->magic!=VXL_S8_JOB_MAGIC||j->version!=1||j->bytes!=128||!j->spin_limit||j->spin_limit>65536)return -EINVAL;
 *s={};s->job=j;s->io=io;s->output=out;
 auto &m=s->meta;m.magic=VXL_S8_RESULT_MAGIC;m.version=1;m.bytes=256;m.case_id=j->case_id;m.generation=j->generation;m.sequence=j->sequence;m.core=core;m.warp=warp;m.owner_warp=j->owner_warp;m.k=j->k;m.flags=j->flags;m.format=1;
 for(unsigned i=0;i<16;++i){m.run_id[i]=j->run_id[i];m.binding_tag[i]=j->binding_tag[i];}return 0;
}
int s8_record(s8_protocol_state *s,uint32_t event,uint32_t index,const s8_words4 &value,uint32_t mask){
 if(!s||s->meta.status_count>=16)return -EOVERFLOW;
 volatile vxl_s8_status_v1 *row=reinterpret_cast<volatile vxl_s8_status_v1 *>(s->output+0x1c0+s->meta.status_count*32);
 uint32_t lane=s->io->participant;
 for(unsigned i=0;i<4;++i)if((lane==4||i==lane)&&(mask&(1u<<i)))row->lane[i]=value.word[i];
 s->io->fence(s->io->context);
 if(leader(s)){row->event_id=event;row->index=index;row->valid_mask=mask;row->reserved0=0;}
 s->meta.status_count++;s->meta.valid_bits|=S8_STATUS_VALID;return 0;
}
int s8_publish(s8_protocol_state *s,int result,uint32_t event){
 if(result){uint32_t code=uint32_t(result<0?-int64_t(result):result);s->meta.sw_status=(event>>16)==0x5048?5:code==EINVAL?1:code==ESTALE?2:code==ETIMEDOUT?7:(code==ERANGE||code==EOVERFLOW)?4:code==EAGAIN?5:3;s->meta.detail=code;s->meta.firsterror_event=event;}
 s->io->fence(s->io->context);
 if(leader(s)){
  volatile uint8_t *out=s->output+0x3c0;
  const uint8_t *in=reinterpret_cast<const uint8_t *>(&s->meta);
  for(unsigned i=0;i<256;++i)if(i<120||i>=124)out[i]=in[i];
  s->io->fence(s->io->context);reinterpret_cast<volatile uint32_t *>(s->output+0x3c0+120)[0]=VXL_S8_DONE;
 }
 s->io->fence(s->io->context);return result;
}
static int status(s8_protocol_state *s,uint32_t event,uint32_t index,const s8_words4 &value,const s8_words4 &expected){int rc=s8_record(s,event,index,value);if(rc)return rc;return s8_same(value,expected)?0:-EPROTO;}
static uint32_t positive_k(uint32_t id){if(id>=101&&id<=105){const uint32_t k[]={1,2,3,2592,4096};return k[id-101];}if(id>=110&&id<=115){const uint32_t k[]={1,3,3,4096,4096,3};return k[id-110];}return id==120?3:0;}
static int phase(s8_protocol_state *s,uint32_t event){return !s->job->flags?0:s->io->phase?s->io->phase(s->io->context,event):-ENODEV;}
static s8_words4 pack(const int16_t *p){s8_words4 words;for(unsigned lane=0;lane<4;++lane)words.word[lane]=uint16_t(p[lane*2])|(uint32_t(uint16_t(p[lane*2+1]))<<16);return words;}
int s8_execute_positive(const vxl_s8_job_v1 *j,uint32_t core,uint32_t warp,const int16_t *a,uint32_t a_bytes,const int16_t *b,uint32_t b_bytes,uint8_t *out,uint32_t out_bytes,const s8_protocol_io *io){
 s8_protocol_state s;int rc=s8_state_init(&s,j,core,warp,out,out_bytes,io);if(rc)return rc;
 if(!(j->core_mask&(1u<<core))||warp!=j->owner_warp)return 0;
 if(!a||!b||!positive_k(j->case_id)||j->k!=positive_k(j->case_id)||a_bytes!=j->k*16||b_bytes!=j->k*16||j->flags>1)return s8_publish(&s,-EINVAL,S8_OP_BEGIN);
 rc=phase(&s,1);if(rc)return s8_publish(&s,rc,0x50480001);
 s8_words4 value=io->begin(io->context,{{j->k,0,0,0}});
 rc=phase(&s,2);if(rc)return s8_publish(&s,rc,0x50480002);
 rc=status(&s,S8_OP_BEGIN,0,value,{{0,0,j->k,1}});if(rc)return s8_publish(&s,rc,S8_OP_BEGIN);
 rc=phase(&s,3);if(rc)return s8_publish(&s,rc,0x50480003);
 for(uint32_t t=0;t<j->k;++t){rc=io->step(io->context,pack(a+t*8),pack(b+t*8));if(rc)return s8_publish(&s,rc,S8_OP_STEP);s.meta.primary_accepted=t+1;}
 rc=phase(&s,4);if(rc)return s8_publish(&s,rc,0x50480004);
 
 uint32_t polls=0;bool ready=false;
 for(;polls<j->spin_limit;++polls){value=io->read(io->context,{{31,0,0,0}});if(s8_same(value,{{0,j->k,j->k,3}})){ready=true;++polls;break;}if(!s8_same(value,{{4,j->k,j->k,1}})){rc=-EPROTO;break;}}
 s.meta.detail=polls;
 int record_rc=s8_record(&s,S8_OP_STATUS,polls,value);if(record_rc)return s8_publish(&s,record_rc,S8_OP_STATUS);
 if(!ready)return s8_publish(&s,rc?rc:-ETIMEDOUT,S8_OP_STATUS);
 rc=phase(&s,5);if(rc)return s8_publish(&s,rc,0x50480005);
 rc=phase(&s,6);if(rc)return s8_publish(&s,rc,0x50480006);
 volatile uint32_t *raw=reinterpret_cast<volatile uint32_t *>(out+64);
 for(uint32_t page=0;page<24;++page){
  value=io->read(io->context,{{page,0,0,0}});
  for(unsigned lane=0;lane<4;++lane)if(io->participant==4||io->participant==lane)raw[page*4+lane]=value.word[lane];
  s.meta.primary_bitmap|=1u<<page;s.meta.pages_count++;
  if(page==0){s8_words4 duplicate=io->read(io->context,{{0,0,0,0}});rc=s8_record(&s,S8_OP_DATA,0,duplicate);if(rc||!s8_same(value,duplicate))return s8_publish(&s,rc?rc:-EPROTO,S8_OP_DATA);}
 }
 io->fence(io->context);s.meta.valid_bits|=S8_RAW_VALID;s.meta.primary_elements=64;
 rc=phase(&s,7);if(rc)return s8_publish(&s,rc,0x50480007);
 if(leader(&s)){rc=s8_device_unpack(reinterpret_cast<const uint32_t *>(out+64),reinterpret_cast<int64_t *>(out+0x4c0));if(rc)return s8_publish(&s,rc,S8_OP_DATA);}
 io->fence(io->context);s.meta.valid_bits|=S8_DECODED_VALID;
 value=io->read(io->context,{{31,0,0,0}});rc=status(&s,S8_OP_STATUS,24,value,{{0,j->k,j->k,7}});if(rc)return s8_publish(&s,rc,S8_OP_STATUS);
 rc=phase(&s,8);if(rc)return s8_publish(&s,rc,0x50480008);
 value=io->read(io->context,{{0x11f,0,0,0}});rc=status(&s,S8_OP_RELEASE,0x11f,value,{{0,j->k,j->k,7}});if(rc)return s8_publish(&s,rc,S8_OP_RELEASE);
 rc=phase(&s,9);if(rc)return s8_publish(&s,rc,0x50480009);
 value=io->read(io->context,{{31,0,0,0}});rc=status(&s,S8_OP_STATUS,31,value,{{0,0,0,0}});if(rc)return s8_publish(&s,rc,S8_OP_STATUS);
 if(j->flags){s.meta.valid_bits|=S8_PHASE_VALID;s.meta.phase_count=12;}
 rc=s8_publish(&s,0,0);if(rc)return rc;
 rc=phase(&s,10);return rc?s8_publish(&s,rc,0x5048000a):0;
}
#if defined(__riscv)
#include "runtime.h"
#include "isa.h"
#include "phase.h"
static s8_words4 gather(uint32_t value){s8_words4 out;for(unsigned lane=0;lane<4;++lane)out.word[lane]=vx_shfl_idx(value,lane,3,0);return out;}
static s8_words4 native_begin(void *,const s8_words4 &control){return gather(s8_isa_begin(control.word[vx_thread_id()]));}
static int native_step(void *,const s8_words4 &a,const s8_words4 &b){uint32_t lane=vx_thread_id();s8_isa_step(a.word[lane],b.word[lane]);return 0;}
static s8_words4 native_read(void *,const s8_words4 &control){return gather(s8_isa_read(control.word[vx_thread_id()]));}
static void native_fence(void *){s8_fence();}
static int native_phase(void *,uint32_t event){return s8_phase_mark(event);}
s8_protocol_io s8_native_io(){return {nullptr,native_begin,native_step,native_read,native_fence,uint32_t(vx_thread_id()),native_phase};}
int s8_run_positive(const vxl_s8_job_v1 *j,uint32_t core,uint32_t warp){
 if(warp!=j->owner_warp)return 0;
 uint32_t slot=core*4+warp;s8_protocol_io io=s8_native_io();
 return s8_execute_positive(j,core,warp,reinterpret_cast<const int16_t *>(uintptr_t(j->a_base+slot*j->input_slot_stride)),j->k*16,reinterpret_cast<const int16_t *>(uintptr_t(j->b_base+slot*j->input_slot_stride)),j->k*16,reinterpret_cast<uint8_t *>(uintptr_t(j->output_base+slot*j->output_stride)),4096,&io);
}
#endif

#include "negative.h"
#include "unpack.h"
#include <errno.h>
static uint32_t event(const s8_protocol_state *s,uint32_t op,bool recovery=false){return ((s->job->case_id&255u)<<16)|(recovery?0x8000u:0u)|op;}
uint32_t s8_negative_status_count(uint32_t id){uint32_t family=id>>8,variant=id&255u;switch(family){case 201:case 208:return variant<2?7:0;case 209:return variant==0?7:0;case 202:return variant==0?12:0;case 203:return variant<7?12:0;case 204:return variant<2?11:0;case 205:return variant==0?11:0;case 206:return variant==0?14:0;case 207:return variant==0?13:0;default:return 0;}}
static int checked(s8_protocol_state *s,uint32_t op,uint32_t index,const s8_words4 &v,const s8_words4 &expected,bool recovery=false,uint32_t mask=15){int rc=s8_record(s,event(s,op,recovery),index,v,mask);if(rc)return rc;for(unsigned lane=0;lane<4;++lane)if((mask&(1u<<lane))&&v.word[lane]!=expected.word[lane])return -EPROTO;return 0;}
static s8_words4 packed(const int16_t *p){s8_words4 v;for(unsigned lane=0;lane<4;++lane)v.word[lane]=uint16_t(p[lane*2])|(uint32_t(uint16_t(p[lane*2+1]))<<16);return v;}
static int steps(s8_protocol_state *s,const int16_t *a,const int16_t *b,uint32_t k){for(uint32_t t=0;t<k;++t){int rc=s->io->step(s->io->context,packed(a+t*8),packed(b+t*8));if(rc)return rc;}return 0;}
static int ready(s8_protocol_state *s,uint32_t k,uint32_t sticky,bool recovery=false){
 s8_words4 v={{0,0,0,0}};uint32_t polls=0;bool complete=false;int rc=0;
 for(;polls<s->job->spin_limit;++polls){v=s->io->read(s->io->context,{{31,0,0,0}});if(s8_same(v,{{sticky,k,k,3}})){complete=true;++polls;break;}if(!s8_same(v,{{sticky?sticky:4u,k,k,1}})){rc=-EPROTO;break;}}
 int saved=s8_record(s,event(s,S8_OP_STATUS,recovery),polls,v);if(saved)return saved;
 return complete?0:rc?rc:-ETIMEDOUT;
}
static void page(s8_protocol_state *s,uint32_t p,bool recovery){
 s8_words4 v=s->io->read(s->io->context,{{p,0,0,0}});volatile uint32_t *out=reinterpret_cast<volatile uint32_t *>(s->output+(recovery?0x900:64));
 for(unsigned lane=0;lane<4;++lane)if(s->io->participant==4||s->io->participant==lane)out[p*4+lane]=v.word[lane];
 if(!recovery){s->meta.primary_bitmap|=1u<<p;s->meta.pages_count++;}
}
static int finish(s8_protocol_state *s,uint32_t k,uint32_t sticky,uint32_t first_page,bool recovery=false){
 for(uint32_t p=first_page;p<24;++p)page(s,p,recovery);
 s->io->fence(s->io->context);
 if(s->io->participant==0||s->io->participant==4){int rc=s8_device_unpack(reinterpret_cast<const uint32_t *>(s->output+(recovery?0x900:64)),reinterpret_cast<int64_t *>(s->output+(recovery?0xa80:0x4c0)));if(rc)return rc;}
 s->io->fence(s->io->context);
 s8_words4 v=s->io->read(s->io->context,{{31,0,0,0}});int rc=checked(s,S8_OP_STATUS,24,v,{{sticky,k,k,7}},recovery);if(rc)return rc;
 v=s->io->read(s->io->context,{{0x11f,0,0,0}});rc=checked(s,S8_OP_RELEASE,0x11f,v,{{0,k,k,7}},recovery);if(rc)return rc;
 v=s->io->read(s->io->context,{{31,0,0,0}});rc=checked(s,S8_OP_STATUS,31,v,{{sticky,0,0,0}},recovery);if(rc)return rc;
 if(recovery){s->meta.valid_bits|=S8_RECOVERY_RAW_VALID|S8_RECOVERY_DECODED_VALID;s->meta.recovery_elements=64;}
 else{s->meta.valid_bits|=S8_RAW_VALID|S8_DECODED_VALID;s->meta.primary_accepted=k;s->meta.primary_elements=64;}
 return 0;
}
static int recovery(s8_protocol_state *s,const int16_t *a,const int16_t *b){
 s8_words4 v=s->io->begin(s->io->context,{{1,0,0,0}});int rc=checked(s,S8_OP_BEGIN,1,v,{{0,0,1,1}},true);if(rc)return rc;
 rc=steps(s,a,b,1);if(rc)return rc;rc=ready(s,1,0,true);if(rc)return rc;return finish(s,1,0,0,true);
}
int s8_execute_peer_phase(s8_protocol_state *s,unsigned phase,const int16_t *a,const int16_t *b){
 if(!s||!s->job||!a||!b)return -EINVAL;
 if(phase==2)return recovery(s,a,b);
 uint32_t family=s->job->case_id>>8;s8_words4 v;int rc;
 if(phase!=1)return -EINVAL;
 if(family==206){
  rc=s->io->step(s->io->context,{{0,0,0,0}},{{0,0,0,0}});if(rc)return rc;
  v=s->io->read(s->io->context,{{31,0,0,0}});rc=checked(s,S8_OP_STEP,31,v,{{3,0,0,0}});if(rc)return rc;
  v=s->io->read(s->io->context,{{0,0,0,0}});rc=checked(s,S8_OP_DATA,0,v,{{0,0,0,0}});if(rc)return rc;
  v=s->io->read(s->io->context,{{31,0,0,0}});return checked(s,S8_OP_STATUS,31,v,{{3,0,0,0}});
 }
 if(family==207){v=s->io->begin(s->io->context,{{1,0,0,0}});rc=checked(s,S8_OP_BEGIN,1,v,{{1,0,0,0}});if(rc)return rc;v=s->io->read(s->io->context,{{31,0,0,0}});return checked(s,S8_OP_STATUS,31,v,{{1,0,0,0}});}
 return -EINVAL;
}
int s8_execute_negative(const vxl_s8_job_v1 *j,uint32_t core,uint32_t warp,const int16_t *a,uint32_t a_bytes,const int16_t *b,uint32_t b_bytes,uint8_t *out,uint32_t out_bytes,const s8_negative_io *ni){
 if(!ni)return -EINVAL;
 s8_protocol_state s;const s8_protocol_io *io=&ni->instruction;int rc=s8_state_init(&s,j,core,warp,out,out_bytes,io);if(rc)return rc;
 if(!(j->core_mask&(1u<<core))||warp!=j->owner_warp)return 0;
 uint32_t family=j->case_id>>8,variant=j->case_id&255u,expected_count=s8_negative_status_count(j->case_id);
 if(!expected_count||j->k!=2||j->flags||!a||!b||a_bytes!=32||b_bytes!=32)return s8_publish(&s,-EINVAL,S8_OP_BEGIN);
 static const uint32_t errors[]={2,4,5,7,6,3,1,8,9},causes[]={S8_OP_BEGIN,S8_OP_DATA,S8_OP_DATA,S8_OP_RELEASE,S8_OP_STEP,S8_OP_STEP,S8_OP_BEGIN,S8_OP_BEGIN,S8_OP_MASK};
 uint32_t code=errors[family-201],cause=causes[family-201],sticky=0,first_page=0;
 s.meta.firsterror_event=event(&s,cause);s.meta.detail=code;
 s8_words4 v;
 if(family==201||family==208||family==209){
  s8_words4 control={{2,0,0,0}};
  if(family==201)control.word[0]=variant?4097:0;
  if(family==208){if(variant)control.word[0]|=1u<<13;else control.word[1]=1;}
  if(family==209){if(!ni->partial_begin)return s8_publish(&s,-ENODEV,event(&s,cause));volatile vxl_s8_status_v1 *row=reinterpret_cast<volatile vxl_s8_status_v1 *>(out+0x1c0);v=ni->partial_begin(io->context,control,row);}
  else v=io->begin(io->context,control);
  rc=checked(&s,cause,control.word[0],v,{{code,0,0,0}},false,family==209?7:15);if(rc)return s8_publish(&s,rc,event(&s,cause));
  v=io->read(io->context,{{31,0,0,0}});rc=checked(&s,S8_OP_STATUS,31,v,{{code,0,0,0}});if(rc)return s8_publish(&s,rc,event(&s,S8_OP_STATUS));
 }else{
  v=io->begin(io->context,{{2,0,0,0}});rc=checked(&s,S8_OP_BEGIN,2,v,{{0,0,2,1}});if(rc)return s8_publish(&s,rc,event(&s,S8_OP_BEGIN));
  if(family==202){v=io->read(io->context,{{0,0,0,0}});rc=checked(&s,S8_OP_DATA,0,v,{{0,0,0,0}});if(rc)return s8_publish(&s,rc,event(&s,cause));v=io->read(io->context,{{31,0,0,0}});rc=checked(&s,S8_OP_STATUS,31,v,{{4,0,2,1}});if(rc)return s8_publish(&s,rc,event(&s,cause));sticky=4;}
  if(family==204&&!variant){v=io->read(io->context,{{0x11f,0,0,0}});rc=checked(&s,S8_OP_RELEASE,0x11f,v,{{7,0,2,1}});if(rc)return s8_publish(&s,rc,event(&s,cause));sticky=7;}
  rc=steps(&s,a,b,2);if(rc)return s8_publish(&s,rc,event(&s,S8_OP_STEP));rc=ready(&s,2,sticky);if(rc)return s8_publish(&s,rc,event(&s,S8_OP_STATUS));
  if(family==203){v=io->read(io->context,{{24+variant,0,0,0}});rc=checked(&s,S8_OP_DATA,24+variant,v,{{0,0,0,0}});if(rc)return s8_publish(&s,rc,event(&s,cause));v=io->read(io->context,{{31,0,0,0}});rc=checked(&s,S8_OP_STATUS,31,v,{{5,2,2,3}});if(rc)return s8_publish(&s,rc,event(&s,cause));sticky=5;}
  if(family==204&&variant){for(unsigned p=0;p<23;++p)page(&s,p,false);first_page=23;v=io->read(io->context,{{0x11f,0,0,0}});rc=checked(&s,S8_OP_RELEASE,0x11f,v,{{7,2,2,3}});if(rc)return s8_publish(&s,rc,event(&s,cause));sticky=7;}
  if(family==205){rc=io->step(io->context,packed(a),packed(b));if(rc)return s8_publish(&s,rc,event(&s,cause));v=io->read(io->context,{{31,0,0,0}});rc=checked(&s,S8_OP_STEP,31,v,{{6,2,2,3}});if(rc)return s8_publish(&s,rc,event(&s,cause));sticky=6;}
  if(family==206||family==207){if(!ni->contender)return s8_publish(&s,-ENODEV,event(&s,cause));rc=ni->contender(io->context,1,&s,a,b);if(rc)return s8_publish(&s,rc,event(&s,cause));v=io->read(io->context,{{31,0,0,0}});rc=checked(&s,S8_OP_STATUS,31,v,{{0,2,2,3}});if(rc)return s8_publish(&s,rc,event(&s,cause));}
  rc=finish(&s,2,sticky,first_page);if(rc)return s8_publish(&s,rc,event(&s,S8_OP_RELEASE));
 }
 if(family==206||family==207)rc=ni->contender(io->context,2,&s,a,b);else rc=recovery(&s,a,b);
 if(rc)return s8_publish(&s,rc,event(&s,S8_OP_BEGIN,true));
 if(s.meta.status_count!=expected_count)return s8_publish(&s,-EPROTO,event(&s,S8_OP_STATUS));
 return s8_publish(&s,0,0);
}
#if defined(__riscv)
#include "runtime.h"
#include "isa.h"
struct s8_native_negative {const vxl_s8_job_v1 *job;uint32_t core;volatile vxl_s8_sync *sync;};
static uint32_t uniform_word(uint32_t word){return vx_shfl_idx(word,0,3,0);}
static int wait_phase(s8_native_negative *n,bool owner,unsigned phase,uint32_t *word){
 for(uint32_t poll=0;poll<n->job->spin_limit;++poll){
  if(uniform_word(n->sync->abort))return -EIO;
  uint32_t seq=uniform_word(n->sync->phase_seq),value=uniform_word(owner?n->sync->contender_done:n->sync->owner_ready);
  if(seq==n->job->sequence && (value>>16)==phase){*word=value;return 0;}
 }
 if(vx_thread_id()==0)n->sync->abort=1;
 s8_fence();return -ETIMEDOUT;
}
static int native_contender(void *context,unsigned phase,s8_protocol_state *s,const int16_t *,const int16_t *){
 auto *n=static_cast<s8_native_negative *>(context);uint32_t start=s->meta.status_count,word=0;
 if(vx_thread_id()==0){n->sync->phase_seq=n->job->sequence;s8_fence();n->sync->owner_ready=(phase<<16)|start;}
 s8_fence();int rc=wait_phase(n,true,phase,&word);if(rc)return rc;
 uint32_t expected=start+(phase==2?5:(n->job->case_id>>8)==206?3:2),end=word&65535u;
 if(end!=expected||end>16)return -EPROTO;
 s->meta.status_count=end;
 if(phase==2){s->meta.recovery_elements=64;s->meta.valid_bits|=S8_RECOVERY_RAW_VALID|S8_RECOVERY_DECODED_VALID;}
 return 0;
}
static s8_words4 native_partial(void *,const s8_words4 &control,volatile vxl_s8_status_v1 *row){
 uint32_t lane=vx_thread_id(),value=control.word[lane];volatile uint32_t *slot=&row->lane[lane];
 /* One assembly region: no C branch, shuffle, vote or barrier runs with a
  * missing lane. Only active lanes store; restore full mask before observing. */
 __asm__ volatile("li t0, 7\n\t.insn r 0x0b, 0, 0, x0, t0, x0\n\tfmv.w.x f10, %0\n\t.insn r 0x0b, 0, 2, x8, x13, x0\n\tfmv.x.w t1, f10\n\tsw t1, 0(%1)\n\tfence iorw, iorw\n\tli t0, 15\n\t.insn r 0x0b, 0, 0, x0, t0, x0"::"r"(value),"r"(slot):"t0","t1",S8_FREG_CLOBBERS);
 s8_words4 result={{0,0,0,0}};for(unsigned i=0;i<3;++i)result.word[i]=row->lane[i];return result;
}
int s8_run_negative(const vxl_s8_job_v1 *j,uint32_t core,uint32_t warp){
 uint32_t family=j->case_id>>8,slot=core*4+j->owner_warp;
 auto *out=reinterpret_cast<uint8_t *>(uintptr_t(j->output_base+slot*j->output_stride));
 auto *a=reinterpret_cast<const int16_t *>(uintptr_t(j->a_base+slot*j->input_slot_stride));
 auto *b=reinterpret_cast<const int16_t *>(uintptr_t(j->b_base+slot*j->input_slot_stride));
 s8_native_negative n={j,core,reinterpret_cast<volatile vxl_s8_sync *>(uintptr_t(j->control_base+64+core*64))};
 s8_negative_io ni={s8_native_io(),native_contender,native_partial};ni.instruction.context=&n;
 if(warp==j->owner_warp)return s8_execute_negative(j,core,warp,a,32,b,32,out,4096,&ni);
 if((family!=206&&family!=207)||warp!=((j->owner_warp+1)&3u))return 0;
 for(unsigned phase=1;phase<=2;++phase){
  uint32_t word=0;int rc=wait_phase(&n,false,phase,&word);if(rc)return rc;
  uint32_t start=word&65535u;if(start>11){if(vx_thread_id()==0)n.sync->abort=1;s8_fence();return -EPROTO;}
  s8_protocol_state s;rc=s8_state_init(&s,j,core,j->owner_warp,out,4096,&ni.instruction);if(rc)return rc;s.meta.status_count=start;
  rc=s8_execute_peer_phase(&s,phase,a,b);
  if(rc&&vx_thread_id()==0)n.sync->abort=1;
  s8_fence();if(vx_thread_id()==0)n.sync->contender_done=(phase<<16)|s.meta.status_count;s8_fence();
  if(rc)return rc;
 }
 return 0;
}
#endif

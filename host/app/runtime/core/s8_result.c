#include <vxl/s8.h>
#include <vxl/s8_runtime.h>
#include <vxl/engine.h>
static int read_result(struct vxl_s8_ctx *c,vxl_u64 address,void *out,vxl_u32 bytes){int rc=vxl_s8_io_bytes(c,address,out,bytes,0);if(!rc)c->results_check_bytes+=bytes;return rc;}
static int phase_check(struct vxl_s8_ctx *c,vxl_u32 core){
 struct vxl_s8_phase_v1 p;const struct vxl_s8_job_v1 *j=&c->job_args;vxl_u64 previous=0,total=0;int rc;
 rc=read_result(c,0x814006c0ULL+(core*4+j->owner_warp)*4096,&p,512);if(rc)return rc;
 if(!j->flags){const vxl_u8 *bytes=(const vxl_u8 *)&p;for(unsigned i=0;i<512;++i)if(bytes[i])return -EPROTO;return 0;}
 if(p.magic!=0x31503853u||p.version!=1||p.bytes!=512||p.valid_mask!=4095||p.core!=core||p.warp!=j->owner_warp||p.width!=44||p.count!=12)return -EPROTO;
 for(unsigned i=0;i<8;++i)if(p.reserved0[i])return -EPROTO;
 for(unsigned i=0;i<256;++i)if(p.reserved[i])return -EPROTO;
 for(unsigned i=0;i<12;++i){vxl_u64 raw=((vxl_u64)p.events[i].raw_hi<<32)|p.events[i].raw_lo;if(p.events[i].event!=i||p.events[i].flags!=1||raw>=(1ULL<<44))return -EPROTO;if(i){vxl_u64 delta=(raw-previous)&((1ULL<<44)-1);if(delta>4000000000ULL||total>4000000000ULL-delta)return -EPROTO;total+=delta;}previous=raw;}
 return 0;
}
static int identities(struct vxl_s8_ctx *c) {
 struct vxl_s8_identity_v1 id;struct vxl_s8_core_done done;struct vxl_s8_global_done global;struct vxl_s8_sync sync;int rc;
 const struct vxl_s8_job_v1 *j=&c->job_args;const struct vxl_s8_layout *layout=&c->program->layout;
 for(vxl_u32 h=0;h<64;++h){
  rc=read_result(c,0x81410040ULL+h*32,&id,32);if(rc)return rc;
  vxl_u32 top=0xffff0000u-h*8192u;
  if(id.core!=h/16||id.warp!=(h%16)/4||id.lane!=h%4||id.hart!=h||id.sp<top-8128u||id.sp>top||(id.sp&15u)||id.tp!=layout->tls_base+h*layout->tbss_stride||id.stack_tag!=(VXL_S8_STACK_TAG^h)||id.tls_tag!=(VXL_S8_TLS_TAG^h))return -ESTALE;
 }
 for(vxl_u32 core=0;core<4;++core){rc=read_result(c,0x81410840ULL+core*32,&done,32);if(rc)return rc;
  if(done.magic!=VXL_S8_CORE_MAGIC||done.version!=1||done.core!=core||done.generation!=j->generation||done.sequence!=j->sequence||done.error||done.harts!=16||done.done!=VXL_S8_DONE)return -ESTALE;}
 rc=read_result(c,0x814108c0ULL,&global,64);if(rc)return rc;
 if(global.magic!=VXL_S8_GLOBAL_MAGIC||global.version!=1||global.bytes!=64||global.generation!=j->generation||global.sequence!=j->sequence||global.cores!=15||global.harts!=64||global.done!=VXL_S8_DONE||memcmp(global.run_id,j->run_id,16)||memcmp(global.binding_tag,j->binding_tag,16))return -ESTALE;
 for(vxl_u32 core=0;core<4;++core){
  rc=read_result(c,0x81411040ULL+core*64,&sync,64);if(rc)return rc;
  if(sync.abort)return -EPROTO;
  for(unsigned i=0;i<12;++i)if(sync.reserved[i])return -EPROTO;
  vxl_u32 family=j->case_id>>8;
  if((family==206||family==207)&&(j->core_mask&(1u<<core))){vxl_u32 count=family==206?14:13;if(sync.phase_seq!=j->sequence||sync.owner_ready!=(0x20000u|(count-5))||sync.contender_done!=(0x20000u|count))return -EPROTO;}
  else if(sync.phase_seq||sync.owner_ready||sync.contender_done)return -EPROTO;
 }
 c->hw.identity_valid=1;return 0;
}
static int metadata(struct vxl_s8_ctx *c) {
 const struct vxl_s8_job_v1 *j=&c->job_args;struct vxl_s8_result_v1 m;int rc;
 c->hw.metadata_count=0;
 if(j->case_id==1){c->hw.metadata_valid=1;return 0;}
 for(vxl_u32 core=0;core<4;++core)if(j->core_mask&(1u<<core)){
  rc=read_result(c,0x814003c0ULL+(core*4+j->owner_warp)*4096,&m,256);if(rc)return rc;
  if(m.magic!=VXL_S8_RESULT_MAGIC||m.version!=1||m.bytes!=256||m.case_id!=j->case_id||m.sw_status||m.generation!=j->generation||m.sequence!=j->sequence||m.core!=core||m.warp!=j->owner_warp||m.owner_warp!=j->owner_warp||m.k!=j->k||m.flags!=j->flags||m.done!=VXL_S8_DONE||m.status_count>16||memcmp(m.run_id,j->run_id,16)||memcmp(m.binding_tag,j->binding_tag,16))return -ESTALE;
  for(unsigned i=0;i<32;++i)if(m.reserved[i])return -EINVAL;
  if(j->case_id>=101&&j->case_id<=120){vxl_u32 flags=S8_RAW_VALID|S8_DECODED_VALID|S8_STATUS_VALID;
   if(j->flags)flags|=S8_PHASE_VALID;
   if(m.format!=1||m.primary_elements!=64||m.primary_accepted!=j->k||m.primary_bitmap!=0xffffff||m.pages_count!=24||m.status_count!=6||m.recovery_elements||m.valid_bits!=flags||m.phase_count!=(j->flags?12u:0u)||m.firsterror_event)return -EPROTO;
  }else if(j->case_id>=2&&j->case_id<=5){vxl_u32 count=j->case_id==2?64:j->case_id==3?32:j->case_id==4?5:4;
   if(m.format!=(j->case_id==5?4:j->case_id)||m.primary_elements!=count||m.primary_accepted||m.primary_bitmap||m.pages_count||m.status_count||m.phase_count||m.recovery_elements||m.valid_bits!=(S8_RAW_VALID)||m.firsterror_event)return -EPROTO;
  }else{
   vxl_u32 family=j->case_id>>8,variant=j->case_id&255u,count,code,cause;
   static const vxl_u32 counts[]={7,12,12,11,11,14,13,7,7},codes[]={2,4,5,7,6,3,1,8,9},causes[]={1,3,3,4,5,5,1,1,6};
   if(family<201||family>209)return -EINVAL;
   count=counts[family-201];code=codes[family-201];cause=causes[family-201];
   int primary=family!=201&&family!=208&&family!=209;
   vxl_u32 valid=S8_STATUS_VALID|S8_RECOVERY_RAW_VALID|S8_RECOVERY_DECODED_VALID;
   if(primary)valid|=S8_RAW_VALID|S8_DECODED_VALID;
   if(m.format!=1||m.valid_bits!=valid||m.status_count!=count||m.phase_count||m.recovery_elements!=64||m.firsterror_event!=((variant<<16)|cause)||m.detail!=code||m.primary_elements!=(primary?64u:0u)||m.primary_accepted!=(primary?2u:0u)||m.primary_bitmap!=(primary?0xffffffu:0u)||m.pages_count!=(primary?24u:0u))return -EPROTO;
  }
  c->hw.metadata_count++;
  rc=phase_check(c,core);if(rc)return rc;
 }
 c->hw.metadata_valid=1;return 0;
}
int vxl_s8_results_check(struct vxl_s8_ctx *c) {
 vxl_u64 begin;int rc;
 if(!c||!c->program||!c->engine)return -ENODEV;
 begin=vxl_s8_now_ns(c);c->results_check_bytes=0;c->hw.identity_valid=c->hw.metadata_valid=0;
 rc=identities(c);if(!rc)rc=metadata(c);
 if(rc&&!c->error_class)c->error_class=VXL_S8_IDENTITY_MISMATCH;
 if(c->io.now_ns)vxl_s8_timing_add(c,VXL_S8_RESULTS_CHECK,begin,vxl_s8_now_ns(c),c->results_check_bytes);
 return rc;
}

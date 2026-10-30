#include <vxl/s8.h>
#include <vxl/engine.h>
static int nonzero(const vxl_u8 *p) {vxl_u8 v=0;for(unsigned i=0;i<16;++i)v|=p[i];return v!=0;}
int vxl_s8_context_init(struct vxl_s8_ctx *c,struct vxl_engine *e,const vxl_u8 tag[16]) {
 if(!c || !e || !tag || e->profile_id!=5 || e->s8 || !nonzero(tag))return -EINVAL;
 memset(c,0,sizeof(*c));c->engine=e;memcpy(c->binding_tag,tag,16);e->s8=c;c->generation=e->generation;c->next_sequence=1;return 0;
}
void vxl_s8_owner_open(struct vxl_s8_ctx *c) {
 if(!c || !c->engine)return;
 c->generation=c->engine->generation;c->next_sequence=1;c->nonce_locked=0;
 memset(c->run_id,0,16);memset(&c->job_args,0,sizeof(c->job_args));
 c->capture_count=0;c->trace_delivered=0;c->error_class=0;memset(&c->hw,0,sizeof(c->hw));
 vxl_s8_timing_reset(c);
}
vxl_u32 vxl_s8_job_crc(const struct vxl_s8_job_v1 *j) {
 const vxl_u8 *bytes=(const vxl_u8 *)j;vxl_u32 crc=0xffffffffu;
 if(!j)return 0;
 for(unsigned i=0;i<128;++i) {
  crc^=(i>=104 && i<108)?0:bytes[i];
  for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));
 }
 return crc^0xffffffffu;
}
static int case_check(const struct vxl_s8_job_v1 *j) {
 vxl_u32 k=0,phase=0;
 if(j->case_id==1 || j->case_id==2 || j->case_id==4 || j->case_id==5)k=0;
 else if(j->case_id==3)k=8;
 else if(j->case_id>=101 && j->case_id<=105) {static const vxl_u32 ks[]={1,2,3,2592,4096};k=ks[j->case_id-101];phase=1;}
 else if(j->case_id>=110 && j->case_id<=115) {static const vxl_u32 ks[]={1,3,3,4096,4096,3};k=ks[j->case_id-110];phase=1;}
 else if(j->case_id==120) {k=3;phase=1;if(j->core_mask!=15)return -EINVAL;}
 else {
  vxl_u32 family=j->case_id>>8,variant=j->case_id&255u,variants;
  if(family<201 || family>209)return -EINVAL;
  variants=(family==201 || family==204 || family==208)?2:(family==203?7:1);
  if(variant>=variants)return -EINVAL;
  k=2;
 }
 return j->k==k && (!j->flags || (phase && j->flags==1))?0:-EINVAL;
}
int vxl_s8_validate_job(struct vxl_s8_ctx *c,const struct vxl_s8_job_v1 *j) {
 if(!c || !j || !c->engine || c->engine->s8!=c || c->engine->profile_id!=5)return -EINVAL;
 if(!c->engine->owner || !c->engine->opened)return -EBADF;
 if(c->generation!=c->engine->generation || j->generation!=c->engine->generation)return -ESTALE;
 if(!c->next_sequence || c->next_sequence==0xffffffffu || j->sequence!=c->next_sequence)return -ESTALE;
 if(j->magic!=VXL_S8_JOB_MAGIC || j->version!=1 || j->bytes!=128 || case_check(j))return -EINVAL;
 if((j->core_mask!=1 && j->core_mask!=2 && j->core_mask!=4 && j->core_mask!=8 && j->core_mask!=15) || j->owner_warp>3)return -EINVAL;
 if(j->a_base!=0x81000040 || j->b_base!=0x81200040 || j->input_slot_stride!=0x20000 || j->output_base!=0x81400000 || j->output_stride!=0x1000 || j->identity_base!=0x81410000 || j->control_base!=0x81411000 || j->spin_limit!=65536)return -EINVAL;
 for(unsigned i=0;i<5;++i)if(j->reserved[i])return -EINVAL;
 if(!nonzero(j->run_id) || memcmp(j->binding_tag,c->binding_tag,16))return -EACCES;
 if(c->nonce_locked && memcmp(j->run_id,c->run_id,16))return -ESTALE;
 if(j->args_crc32!=vxl_s8_job_crc(j))return -EBADMSG;
 return 0;
}
int vxl_s8_canonical_job(struct vxl_s8_ctx *c,const struct vxl_s8_job_v1 *input,struct vxl_s8_job_v1 *output) {
 int rc;if(!output)return -EINVAL;rc=vxl_s8_validate_job(c,input);if(rc)return rc;
 *output=*input;output->generation=c->engine->generation;output->sequence=c->next_sequence;
 output->args_crc32=vxl_s8_job_crc(output);return 0;
}
int vxl_s8_accept_job(struct vxl_s8_ctx *c,const struct vxl_s8_job_v1 *j) {
 int rc=vxl_s8_validate_job(c,j);if(rc)return rc;
 c->job_args=*j;memcpy(c->run_id,j->run_id,16);c->nonce_locked=1;c->next_sequence++;return 0;
}
int vxl_s8_reserve_check(const struct vxl_request *r,int reserve) {
 static const struct vxl_s8_region fixed[8]={{0x80000000,0x100000},{0x80100000,4096},{0x81000000,0x200000},{0x81200000,0x200000},{0x81400000,65536},{0x81410000,4096},{0x81411000,4096},{0xfff70000,0x80040}};
 if(!r || !reserve || r->domain!=VXL_PL)return -EACCES;
 for(unsigned i=0;i<8;++i) {
  vxl_u32 slots=(i>=2 && i<=4)?16:1,purpose=i==0?VXL_CODE:(i==1?VXL_ARGS:VXL_DATA);
  vxl_u64 size=fixed[i].size/slots;
  if(r->purpose!=purpose || r->length!=size)continue;
  for(vxl_u32 slot=0;slot<slots;++slot)if(r->address==fixed[i].begin+slot*size)return 0;
 }
 return -EACCES;
}

#include <vxl/hunet.h>
#include "../generated/hunet/catalog.h"
#include "../generated/hunet/layout.h"
const vxl_u32 *vxl_hunet_catalog_job(vxl_u32 seq){return seq>=1&&seq<=1023?vxl_hunet_jobs[seq-1]:0;}
const struct vxl_hunet_matrix_v1 *vxl_hunet_catalog_matrix(vxl_u32 i){return i<314?&vxl_hunet_matrices[i]:0;}
const vxl_u8 *vxl_hunet_catalog_aux(void){return vxl_hunet_aux_bytes;}
static int mul(vxl_u64 a,vxl_u64 b,vxl_u64 *n){if(b&&a>~(vxl_u64)0/b)return -EOVERFLOW;*n=a*b;return 0;}
static int span(vxl_u64 a,vxl_u64 n,int write){
 unsigned i;if(!a||!n||a>0x100000000ULL||n>0x100000000ULL-a)return -ERANGE;
 if(!write&&a>=VXL_HUNET_WEIGHTS&&!vxl_range_check(23685056,a-VXL_HUNET_WEIGHTS,n))return 0;
 for(i=0;i<106;i++)if(a>=vxl_hunet_slots[i].start&&!vxl_range_check(vxl_hunet_slots[i].bytes,a-vxl_hunet_slots[i].start,n)&&(!write||vxl_hunet_slots[i].writable))return 0;
 return -EACCES;
}
static int elements(vxl_u64 a,vxl_u64 count,vxl_u64 bytes,int write){vxl_u64 n;int rc=mul(count,bytes,&n);return rc?rc:span(a,n,write);}
#define CHECK(x) do {int rc_=(x);if(rc_)return rc_;}while(0)
int vxl_hunet_validate_spans(const struct vxl_hunet_job_v1 *j,const struct vxl_hunet_matrix_v1 *m){
 vxl_u64 hw,ihw,ohw,input,output,nn,wn,tiles,shift;
 if(!j||!j->ci||!j->co||!j->ih||!j->iw||!j->oh||!j->ow||!j->tasks||j->op<1||j->op>13||j->dtype!=1||j->reserved)return -EINVAL;
 CHECK(mul(j->ih,j->iw,&hw));ihw=hw;CHECK(mul(j->oh,j->ow,&ohw));CHECK(mul(j->ci,ihw,&input));CHECK(mul(j->co,ohw,&output));
 if(j->op<=3){
  if(!m||!m->m||!m->n||!m->k||m->mp<m->m||m->kp<m->k||!m->count||m->np<m->count||m->mp%8||m->np%4||m->kp%8||m->offset>m->n||m->count>m->n-m->offset)return -EINVAL;
  if(j->flags!=0&&j->flags!=2)return -EINVAL;
  CHECK(elements(j->aux,1,32,0));CHECK(mul(m->kp,m->np,&nn));CHECK(mul(m->mp,m->kp,&wn));CHECK(mul(m->mp,m->np,&tiles));tiles/=32;
  if(j->flags==2){if(m->m!=1||m->n!=j->iw||m->k!=j->k)return -EINVAL;}
  else{vxl_u64 kk;CHECK(mul(j->k,j->k,&kk));CHECK(mul(kk,j->ci,&kk));if(m->m!=j->co||m->n!=ohw||m->k!=kk)return -EINVAL;}
  if(j->op==1){if(j->tasks!=nn)return -EINVAL;CHECK(elements(j->x,input,4,0));CHECK(elements(j->y,nn,2,1));}
  else if(j->op==2){if(j->tasks!=tiles)return -EINVAL;CHECK(elements(j->x,nn,2,0));CHECK(elements(j->w,wn,2,0));CHECK(elements(j->y,(vxl_u64)m->mp*m->np,4,1));CHECK(elements(j->scratch,tiles,4,1));}
  else{if(j->tasks!=(vxl_u64)m->m*m->count)return -EINVAL;CHECK(elements(j->x,(vxl_u64)m->m*m->np,4,0));CHECK(elements(j->y,output,4,1));if(j->bias)CHECK(elements(j->bias,m->m,4,0));}
  return 0;
 }
 if(j->flags)return -EINVAL;
 switch(j->op){
 case 4:if(j->tasks!=1)return -EINVAL;CHECK(elements(j->x,input,4,0));return elements(j->y,2,4,1);
 case 5:if(j->tasks!=input)return -EINVAL;CHECK(elements(j->gamma,j->ci,4,0));CHECK(elements(j->beta,j->ci,4,0));CHECK(elements(j->scratch,2,4,0));break;
 case 6:case 10:if(j->tasks!=input)return -EINVAL;break;
 case 7:if(!j->n_div||j->ci%j->n_div||j->tasks!=input)return -EINVAL;break;
 case 8:if(j->tasks!=input)return -EINVAL;CHECK(elements(j->w,j->tasks,4,0));break;
 case 9:if(j->tasks!=j->ci)return -EINVAL;CHECK(elements(j->x,input,4,0));return elements(j->y,j->tasks,4,1);
 case 11:if(j->tasks!=output)return -EINVAL;CHECK(elements(j->w,j->co,4,0));break;
 case 12:CHECK(mul(j->groups,hw,&shift));if(shift>input||j->tasks>input-shift)return -ERANGE;CHECK(elements((vxl_u64)j->x+shift*4,j->tasks,4,0));return elements(j->y,j->tasks,4,1);
 case 13:if(j->tasks<=input)return -EINVAL;CHECK(elements(j->x,input,4,0));CHECK(elements(j->w,j->tasks-input,4,0));return elements(j->y,j->tasks,4,1);
 default:return -EINVAL;
 }
 CHECK(elements(j->x,j->tasks,4,0));return elements(j->y,j->tasks,4,1);
}
int vxl_hunet_validate_job(const struct vxl_hunet_ctx *c,const struct vxl_hunet_job_v1 *j,vxl_u32 seq){
 const struct vxl_hunet_matrix_v1 *m=0;
 if(!c||(c->program_id!=6&&(c->program_id!=8||!c->hybrid.enabled))||!j||!seq||seq>1023||j->sequence!=seq||j->magic!=0x314e5548||j->abi!=1||j->bytes!=128||j->stage_id>99)return -EINVAL;
 if(memcmp(j,vxl_hunet_catalog_job(seq),128))return -EACCES;
 if(j->op<=3){if(j->aux<VXL_HUNET_AUX||(j->aux-VXL_HUNET_AUX)%32)return -EINVAL;m=vxl_hunet_catalog_matrix((j->aux-VXL_HUNET_AUX)/32);}
 return vxl_hunet_validate_spans(j,m);
}

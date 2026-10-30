#include <vxl/hunet_opt.h>
#include <vxl/hunet.h>
static int zero(const vxl_u8 *p,size_t n){size_t i;for(i=0;i<n;i++)if(p[i])return 0;return 1;}
static int address(vxl_u32 p){return !p||(p>=0x80800000u&&p<0x84800000u)||(p>=0x85000000u&&p<0x87067000u);}
int vxl_opt_payload_validate(vxl_u32 cmd,const void *payload,size_t bytes){
 const struct vxl_opt_binding_v4 *b=payload;const struct vxl_hunet_binding_v1 *v;size_t i;
 if(cmd!=VXL_HYBRID_GET_BINDING)return -ENOTTY;
 if(!b)return -EFAULT;
 if(bytes!=sizeof(*b))return -EINVAL;
 v=&b->v3.v2.v1;
 if(v->magic!=VXL_HUNET_BINDING_MAGIC||v->version!=4||v->bytes!=512||v->profile_id!=4||v->program_id!=10||v->features!=31)return -EINVAL;
 if(!zero(b->v3.v2.reserved,32)||!zero(b->v3.reserved,32))return -EINVAL;
 for(i=0;i<7;i++)if(zero((const vxl_u8 *)v+32+i*32,32))return -EINVAL;
 if(zero(b->v3.v2.cpu_binary_sha256,32)||zero(b->v3.v2.cpu_contract_sha256,32)||zero(b->v3.v2.cpu_stages_sha256,32)||zero(b->v3.fusion_contract_sha256,32)||zero(b->v3.descriptors_sha256,32)||zero(b->v3.layout_sha256,32))return -EINVAL;
 return 0;
}
int vxl_opt_graph_validate(const void *job,size_t bytes,const void *descriptor,size_t dbytes){
 vxl_u32 w[32],h[32],n[24],i,j,previous,stage;
 if(!job||!descriptor)return -EFAULT;
 if(bytes!=128||dbytes<128||dbytes>4096)return -EINVAL;
 /* Host and target are little endian; memcpy avoids alignment assumptions. */
 memcpy(w,job,128);memcpy(h,descriptor,128);
 if(w[0]!=VXL_OPT_JOB_MAGIC||w[1]!=2||w[2]!=128||(w[3]!=16&&w[3]!=17))return -EINVAL;
 if(!w[4]||w[4]>w[5]||w[5]>1023||w[6]>w[7]||w[7]>=100)return -EINVAL;
 if(!w[8]||w[8]>64||w[9]!=0x84000000u+(w[8]-1)*4096u||w[10]!=dbytes||(dbytes&63u))return -EINVAL;
 if(w[11]!=0x86c4df40u||w[12]!=4096||w[15]!=2||!zero((vxl_u8 *)(w+16),64))return -EINVAL;
 if(!w[14]){if(w[13])return -EINVAL;}
 else if(w[14]>262144||w[13]<0x84040040u||(w[13]&3u)||(vxl_u64)w[13]+(vxl_u64)w[14]*4>0x84140040ULL)return -EINVAL;
 if(h[0]!=VXL_OPT_DESC_MAGIC||h[1]!=2||h[2]!=dbytes||h[3]!=w[8]||memcmp(h+4,w+4,16)||h[9]!=2||h[10]!=w[11]||h[11]!=w[12]||!zero((vxl_u8 *)(h+12),80))return -EINVAL;
 if(!h[8]||h[8]>32||((128+96*h[8]+63)&~63u)!=dbytes)return -EINVAL;
 if(!zero((const vxl_u8 *)descriptor+128+96*h[8],dbytes-128-96*h[8]))return -EINVAL;
 previous=w[4]-1;stage=w[6];
 for(i=0;i<h[8];i++){
  memcpy(n,(const vxl_u8 *)descriptor+128+96*i,96);
  if(!n[0]||n[0]>15||n[0]==6||n[2]!=previous+1||n[2]>n[3]||n[3]>w[5]||n[1]<stage||n[1]>w[7])return -EINVAL;
  for(j=4;j<12;j++)if(!address(n[j]))return -EINVAL;
  /* Avoid multiplication overflow before bounding each tensor dimension. */
  for(j=12;j<=17;j++)if(n[j]>8494080u)return -EINVAL;
  if((vxl_u64)n[12]*n[14]>8494080u||(vxl_u64)n[13]*n[16]>8494080u)return -EINVAL;
  if((vxl_u64)n[12]*n[14]*n[15]>8494080u||(vxl_u64)n[13]*n[16]*n[17]>8494080u)return -EINVAL;
  if((i==0&&n[1]!=w[6])||(w[3]==16&&(h[8]!=2||n[0]!=(i?5u:4u))))return -EINVAL;
  previous=n[3];stage=n[1];
 }
 return previous==w[5]&&stage==w[7]?0:-EINVAL;
}
int vxl_opt_descriptor_matches(const struct vxl_opt_catalog *cat,const void *job,const void *descriptor,size_t bytes){
 vxl_u32 w[32];vxl_u64 offset;
 if(!cat||!job||!descriptor)return -EINVAL;
 memcpy(w,job,128);if(!w[8]||w[8]>64)return -EINVAL;offset=(vxl_u64)(w[8]-1)*4096;
 if(!cat->graph_data||offset+bytes>cat->graph_bytes||bytes!=w[10])return -EACCES;
 if(memcmp(cat->graph_data+offset,descriptor,bytes))return -EACCES;
 return vxl_opt_graph_validate(job,128,descriptor,bytes);
}
int vxl_opt_validate_job(const struct vxl_hunet_ctx *c,const void *job,vxl_u32 next,struct vxl_opt_meta *out){
 const struct vxl_opt_catalog *cat;unsigned i;int rc;
 if(!c||!job||!out||!next||c->program_id!=10||!c->opt.release||!c->opt.release->catalog)return -EINVAL;
 cat=c->opt.release->catalog;
 for(i=0;i<cat->gpu_jobs;i++)if(!memcmp(job,cat->jobs[i],128)){
  const struct vxl_opt_meta *m=&cat->metadata[i];if(m->first!=next)return -EACCES;
  if(m->op==16||m->op==17){vxl_u64 off=(vxl_u64)(m->block_id-1)*4096;if(off+cat->jobs[i][10]>cat->graph_bytes)return -EACCES;rc=vxl_opt_descriptor_matches(cat,job,cat->graph_data+off,cat->jobs[i][10]);if(rc)return rc;}
  else if(m->op!=15){struct vxl_hunet_job_v1 j;memcpy(&j,job,128);if(j.op<=3||j.op==6)return -EACCES;rc=vxl_hunet_validate_spans(&j,NULL);if(rc)return rc;}
  *out=*m;return 0;
 }
 return -EACCES;
}

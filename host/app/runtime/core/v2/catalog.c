#include <vxl/v2/program.h>
#include <vxl/hunet.h>
static int zero(const void *p,vxl_u32 n){const vxl_u8 *b=p;vxl_u32 i;for(i=0;i<n;i++)if(b[i])return 0;return 1;}
static int namespace_allowed(const struct vxl_v2_provider *p,const struct vxl_v2_catalog_entry *e){
 if(p->flags==1)return e->namespace_id==1 || e->namespace_id==3;
 if(p->flags==2)return e->namespace_id==2;
 if(p->flags!=4)return 0;
 if(e->namespace_id==1)return e->logical_profile==1 && (e->logical_program==65552 || e->logical_program==65568);
 if(e->namespace_id==2)return e->logical_profile==5 && e->logical_program>=9 && e->logical_program<=12;
 return e->namespace_id==3 && e->logical_profile==256 && e->logical_program>=512 && e->logical_program<=521;
}
static int scalar_at(const vxl_u8 *raw,vxl_u32 count,vxl_u32 off){
 vxl_u32 i;struct vxl_v2_schema_field f;
 for(i=0;i<count;i++){memcpy(&f,raw+64+64*i,64);if(f.kind==VXL_V2_SCALAR32 && off>=f.arg_offset && off-f.arg_offset<(f.constant_bytes?f.constant_bytes:4) && !(off&3))return 1;}
 return 0;
}
static int address_ref(const vxl_u8 *raw,vxl_u32 count,vxl_u32 ref){
 vxl_u32 i;struct vxl_v2_schema_field f;
 for(i=0;i<count;i++){memcpy(&f,raw+64+64*i,64);if(f.kind==VXL_V2_ADDRESS32 && f.data_ref==ref)return 1;}
 return 0;
}
static int predicate_check(const vxl_u8 *raw,vxl_u32 count,const struct vxl_v2_schema_predicate *p){
 const vxl_u32 *a=p->parameters;
 if(!zero(p->reserved,sizeof(p->reserved)))return -EINVAL;
 if(p->operation==VXL_V2_REFS_DISJOINT){
  if(p->arg_offset!=VXL_V2_ABSENT || p->rhs_offset!=VXL_V2_ABSENT || p->aux_offset!=VXL_V2_ABSENT || a[0]>=8 || a[1]>=8 || a[0]==a[1] || !address_ref(raw,count,a[0]) || !address_ref(raw,count,a[1]) || !zero(a+2,24))return -EINVAL;
  return 0;
 }
 if(!scalar_at(raw,count,p->arg_offset))return -EINVAL;
 if(p->operation==VXL_V2_SUM_LE){
  if(!scalar_at(raw,count,p->rhs_offset) || !scalar_at(raw,count,p->aux_offset) || !zero(a,32))return -EINVAL;
 }else if(p->operation==VXL_V2_PAIR_SET){
  if(!scalar_at(raw,count,p->rhs_offset) || p->aux_offset!=VXL_V2_ABSENT || !zero(a+4,16) || (a[0]==a[2] && a[1]==a[3]))return -EINVAL;
 }else if(p->operation==VXL_V2_SET3){
  if(p->rhs_offset!=VXL_V2_ABSENT || p->aux_offset!=VXL_V2_ABSENT || !(a[0]<a[1] && a[1]<a[2]) || !zero(a+3,20))return -EINVAL;
 }else if(p->operation==VXL_V2_STRIDED_SET){
  if(p->rhs_offset!=VXL_V2_ABSENT || p->aux_offset!=VXL_V2_ABSENT || !a[1] || !a[2] || (vxl_u64)a[0]+(vxl_u64)(a[2]-1)*a[1]>0xffffffffULL || !zero(a+3,20))return -EINVAL;
 }else return -EINVAL;
 return 0;
}
int vxl_v2_schema_check(const void *bytes,vxl_u32 n,vxl_u32 args){
 const vxl_u8 *raw=bytes;struct vxl_v2_schema_header h;struct vxl_v2_schema_field f;vxl_u32 cover[32]={0},i,k,words,first;
 if(!raw || n<64 || args>4096 || !args || (args&3))return -EINVAL;
 memcpy(&h,raw,64);
 if(h.magic!=VXL_V2_SCHEMA_MAGIC || (h.version!=1 && h.version!=2) || h.bytes!=n || h.args_bytes!=args || !h.field_count || h.field_count>64 || n!=64+64*h.field_count || !zero(h.reserved,sizeof(h.reserved)))return -EINVAL;
 if((h.provider_flags!=1 && h.provider_flags!=2) || !h.code_gpu || (h.code_gpu&63) || (h.rodata_gpu&63) || (h.crt_gpu&63) || (h.stack_gpu&63) || (h.crt_bytes&63) || (h.stack_bytes&63) || (!!h.crt_bytes!=!!h.crt_gpu) || (!!h.stack_bytes!=!!h.stack_gpu))return -EINVAL;
 for(i=0;i<h.field_count;i++){
  memcpy(&f,raw+64+64*i,64);
  if(f.kind==VXL_V2_PREDICATE32){
   struct vxl_v2_schema_predicate p;memcpy(&p,&f,64);
   if(h.version!=2 || predicate_check(raw,h.field_count,&p))return -EINVAL;
   continue; /* A predicate reads already-covered scalars, never covers bytes. */
  }
  if(!zero(f.reserved,sizeof(f.reserved)) || (f.arg_offset&3) || f.arg_offset>=args)return -EINVAL;
  words=1;
  if(f.kind==VXL_V2_SCALAR32){
   if(f.data_ref || f.access || f.alignment || f.element_bytes || !zero(f.count_offsets,sizeof(f.count_offsets)) || f.relative_arg_offset || f.min_value>f.max_value)return -EINVAL;
   if(f.constant_bytes){if(f.constant_bytes&3)return -EINVAL;words=f.constant_bytes/4;}
  }else if(f.kind==VXL_V2_ZERO_RANGE){
   if(!f.constant_bytes || (f.constant_bytes&3) || f.data_ref || f.access || f.alignment || f.element_bytes || !zero(f.count_offsets,sizeof(f.count_offsets)) || f.relative_arg_offset || f.min_value || f.max_value)return -EINVAL;
   words=f.constant_bytes/4;
  }else if(f.kind==VXL_V2_ADDRESS32){
   if(f.data_ref>=8 || !f.access || (f.access & ~3u) || !f.alignment || f.alignment>4096 || (f.alignment & (f.alignment-1)) || (!f.element_bytes && !f.constant_bytes) || f.min_value || f.max_value)return -EINVAL;
   for(k=0;k<3;k++)if(f.count_offsets[k]!=VXL_V2_ABSENT && !scalar_at(raw,h.field_count,f.count_offsets[k]))return -EINVAL;
   if(f.relative_arg_offset!=VXL_V2_ABSENT && !scalar_at(raw,h.field_count,f.relative_arg_offset))return -EINVAL;
  }else return -EINVAL;
  if(!words || words>args/4 || f.arg_offset/4>args/4-words)return -EINVAL;
  first=f.arg_offset/4;
  for(k=first;k<first+words;k++){vxl_u32 bit=1u<<(k%32);if(cover[k/32]&bit)return -EINVAL;cover[k/32]|=bit;}
 }
 for(i=0;i<args/4;i++)if(!(cover[i/32] & (1u<<(i%32))))return -EINVAL;
 return 0;
}
static int validate_entry(const vxl_u8 *raw,vxl_u32 n,vxl_u32 count,const struct vxl_v2_catalog_entry *e,const struct vxl_v2_provider *p){
 struct vxl_v2_schema_header s;struct vxl_v2_catalog_entry tmp;struct vxl_hunet_sha sha;vxl_u8 sum[32];vxl_u64 end;
 if(e->flags || e->reserved0 || !zero(e->reserved,sizeof(e->reserved)) || !e->namespace_id || !e->logical_profile || !e->logical_program || !e->code_bytes || (e->code_bytes&3) || e->args_bytes>4096 || !e->args_bytes || e->reloc_count>64)return -EINVAL;
 if((vxl_u64)e->code_bytes+e->rodata_bytes>8u*1024u*1024u || (vxl_u64)e->code_offset+e->code_bytes>0xffffffffULL || (vxl_u64)e->rodata_offset+e->rodata_bytes>0xffffffffULL)return -EINVAL;
 if(e->rodata_bytes && (vxl_u64)e->code_offset+e->code_bytes>e->rodata_offset && (vxl_u64)e->rodata_offset+e->rodata_bytes>e->code_offset)return -EINVAL;
 if(e->schema_offset<64+count*512 || (e->schema_offset&3) || e->schema_bytes<64 || (vxl_u64)e->schema_offset+e->schema_bytes>n)return -EINVAL;
 if(e->provider_flags!=p->flags || memcmp(e->hardware_contract,p->hardware_contract,32) || memcmp(e->bit,p->bit,32) || memcmp(e->xsa,p->xsa,32))return -EXDEV;
 if(!namespace_allowed(p,e))return -EXDEV;
 if(vxl_v2_schema_check(raw+e->schema_offset,e->schema_bytes,e->args_bytes))return -EINVAL;
 memcpy(&s,raw+e->schema_offset,64);
 end=(vxl_u64)s.code_gpu+e->code_bytes;
 if(s.provider_flags!=(p->flags==4?(e->namespace_id==2?2u:1u):p->flags) || s.field_count!=e->reloc_count || e->entry_pc<s.code_gpu || e->entry_pc>=end || end>0x100000000ULL || (e->entry_pc&3) || !e->args_gpu || (e->args_gpu&63) || (!!e->rodata_bytes!=!!s.rodata_gpu))return -EINVAL;
 vxl_hunet_hash(raw+e->schema_offset,e->schema_bytes,sum);if(memcmp(sum,e->schema,32))return -EBADMSG;
 tmp=*e;memset(tmp.entry_id,0,32);vxl_hunet_sha_init(&sha);vxl_hunet_sha_update(&sha,&tmp,512);vxl_hunet_sha_update(&sha,raw+e->schema_offset,e->schema_bytes);vxl_hunet_sha_final(&sha,sum);
 return memcmp(sum,e->entry_id,32)?-EBADMSG:0;
}
int vxl_v2_catalog_open(struct vxl_v2_catalog *c,const void *bytes,vxl_u32 n,const struct vxl_v2_provider *p){
 const vxl_u8 *raw=bytes;const vxl_u32 endian=1;struct vxl_v2_catalog_header h;struct vxl_v2_catalog_entry e,previous;vxl_u8 sum[32];vxl_u32 i,j;int rc;
 if(*(const vxl_u8 *)&endian!=1)return -EOPNOTSUPP;
 if(!c || !p || !raw || n<64 || n>65536)return -EINVAL;
 memcpy(&h,raw,64);
 if(h.magic!=VXL_V2_CATALOG_MAGIC || h.version!=1 || h.total_bytes!=n || !h.count || h.count>16 || h.entry_bytes!=512 || 64+h.count*512>n || !zero(h.reserved,sizeof(h.reserved)))return -EINVAL;
 vxl_hunet_hash(raw+64,n-64,sum);if(memcmp(sum,h.payload_sha,32))return -EBADMSG;
 for(i=0;i<h.count;i++){
  memcpy(&e,raw+64+i*512,512);rc=validate_entry(raw,n,h.count,&e,p);if(rc)return rc;
  for(j=0;j<i;j++){
   memcpy(&previous,raw+64+j*512,512);
   if(!memcmp(previous.entry_id,e.entry_id,32))return -EEXIST;
   if(e.schema_offset<(vxl_u64)previous.schema_offset+previous.schema_bytes && previous.schema_offset<(vxl_u64)e.schema_offset+e.schema_bytes && (e.schema_offset!=previous.schema_offset || e.schema_bytes!=previous.schema_bytes))return -EINVAL;
  }
 }
 c->bytes=raw;c->length=n;c->count=h.count;c->provider=*p;vxl_hunet_hash(raw,n,c->sha);return 0;
}
int vxl_v2_catalog_lookup(const struct vxl_v2_catalog *c,const vxl_u8 id[32],struct vxl_v2_catalog_entry *out){
 struct vxl_v2_catalog_entry e;vxl_u8 sha[32];vxl_u32 i;
 if(!c || !c->bytes || !out || !id || c->length>65536 || c->count>16)return -EINVAL;
 vxl_hunet_hash(c->bytes,c->length,sha);if(memcmp(sha,c->sha,32))return -EBADMSG;
 for(i=0;i<c->count;i++){memcpy(&e,c->bytes+64+i*512,512);if(!memcmp(id,e.entry_id,32)){*out=e;return 0;}}
 return -ENOENT;
}

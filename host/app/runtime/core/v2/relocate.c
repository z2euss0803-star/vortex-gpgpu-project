#include <vxl/v2/program.h>
static vxl_u32 get32(const vxl_u8 *b,vxl_u32 off){vxl_u32 v;memcpy(&v,b+off,4);return v;}
static int predicate(const struct vxl_v2_schema_predicate *p,const vxl_u8 *b,const struct vxl_v2_job *j){
 const vxl_u32 *a=p->parameters;vxl_u64 x,y;
 if(p->operation==VXL_V2_REFS_DISJOINT){
  const struct vxl_v2_ref *left,*right;
  if(a[0]>=j->num_refs || a[1]>=j->num_refs)return -EINVAL;
  left=j->refs+a[0];right=j->refs+a[1];
  if(left->handle==right->handle && left->offset<right->offset+right->length && right->offset<left->offset+left->length)return -ERANGE;
  return 0;
 }
 x=get32(b,p->arg_offset);
 if(p->operation==VXL_V2_SUM_LE)return x+get32(b,p->rhs_offset)<=get32(b,p->aux_offset)?0:-ERANGE;
 if(p->operation==VXL_V2_PAIR_SET){y=get32(b,p->rhs_offset);return ((x==a[0] && y==a[1]) || (x==a[2] && y==a[3]))?0:-ERANGE;}
 if(p->operation==VXL_V2_SET3)return (x==a[0] || x==a[1] || x==a[2])?0:-ERANGE;
 if(p->operation==VXL_V2_STRIDED_SET)return (x>=a[0] && (x-a[0])%a[1]==0 && (x-a[0])/a[1]<a[2])?0:-ERANGE;
 return -EINVAL;
}
int vxl_v2_build_args(struct vxl_v2_ctx *c,const struct vxl_v2_program *p,const struct vxl_v2_job *j,void *out,vxl_u32 max){
 struct vxl_v2_catalog_entry trusted;struct vxl_v2_schema_header h;struct vxl_v2_schema_field f;struct vxl_v2_ref args;struct vxl_v2_object *arg_object=0;vxl_u8 *b=out;const vxl_u8 *schema;vxl_u32 i,k,domain=0;int rc,found=0;
 if(!c || !p || !j || !out || !c->programs)return -EINVAL;
 for(i=0;i<16;i++)if(p==&c->programs->programs[i])found=1;
 if(!found || !p->ref.handle || !p->users)return -EBADF;
 rc=vxl_v2_owner_check(c,c->owner,j->device_generation);if(rc)return rc;
 rc=vxl_v2_job_valid(j);if(rc)return rc;
 if(j->program_handle!=p->ref.handle || j->program_generation!=p->ref.generation || p->ref.device_generation!=j->device_generation)return -ESTALE;
 rc=vxl_v2_catalog_lookup(&c->programs->catalog,p->ref.entry_id,&trusted);if(rc)return rc;
 if(memcmp(&trusted,&p->entry,512))return -EKEYREJECTED;
 if(j->args_bytes!=p->entry.args_bytes || max<j->args_bytes)return -EINVAL;
 for(i=0;i<64;i++)if(c->objects[i].handle==j->args_handle)domain=c->objects[i].domain;
 args=(struct vxl_v2_ref){j->args_handle,j->args_generation,j->args_offset,j->args_bytes,1,domain,j->device_generation};
 rc=vxl_v2_ref_resolve(c,c->owner,&args,1,&arg_object);if(rc)return rc;
 if(arg_object->purpose!=VXL_V2_ARGS || arg_object->sealed)return -EACCES;
 /* Args are snapshotted privately under the device mutex. Existing read pins
  * do not prohibit another read; public COPY remains blocked by GPU pins.
  * ARGS/CODE objects may never be supplied as writable job data below. */
 for(i=0;i<j->num_refs;i++){struct vxl_v2_object *o;rc=vxl_v2_ref_resolve(c,c->owner,&j->refs[i],j->refs[i].access,&o);if(rc)return rc;if(o->purpose!=VXL_V2_DATA && o->purpose!=VXL_V2_READONLY)return -EACCES;}
 if(!c->ops || !c->ops->copy)return -ENODEV;
 rc=c->ops->copy(c->memory_context,arg_object,args.offset,b,args.length,0);
 if(rc){if(rc==-EIO || rc==-ETIMEDOUT)c->quarantined=1;return rc;}
 schema=c->programs->catalog.bytes+trusted.schema_offset;memcpy(&h,schema,64);
 /* Validate every scalar first; address extents only consume validated scalars. */
 for(i=0;i<h.field_count;i++){
  vxl_u32 bytes;memcpy(&f,schema+64+64*i,64);bytes=f.constant_bytes?f.constant_bytes:4;
  if(f.kind==VXL_V2_SCALAR32){for(k=0;k<bytes;k+=4){vxl_u32 v=get32(b,f.arg_offset+k);if(v<f.min_value || v>f.max_value)return -ERANGE;}}
  if(f.kind==VXL_V2_ZERO_RANGE){for(k=0;k<bytes;k++)if(b[f.arg_offset+k])return -EINVAL;}
 }
 for(i=0;i<h.field_count;i++){
  memcpy(&f,schema+64+64*i,64);
  if(f.kind==VXL_V2_PREDICATE32){struct vxl_v2_schema_predicate p;memcpy(&p,&f,64);rc=predicate(&p,b,j);if(rc)return rc;}
 }
 for(i=0;i<h.field_count;i++){
  struct vxl_v2_object *o;const struct vxl_v2_ref *ref;vxl_u64 amount,relative=0,gpu;
  memcpy(&f,schema+64+64*i,64);if(f.kind!=VXL_V2_ADDRESS32)continue;
  if(f.data_ref>=j->num_refs || get32(b,f.arg_offset))return -EINVAL;
  ref=&j->refs[f.data_ref];rc=vxl_v2_ref_resolve(c,c->owner,ref,f.access,&o);if(rc)return rc;
  amount=f.element_bytes;
  for(k=0;k<3;k++)if(f.count_offsets[k]!=VXL_V2_ABSENT){vxl_u32 count=get32(b,f.count_offsets[k]);if(!count || (amount && count>~(vxl_u64)0/amount))return -EOVERFLOW;amount*=count;}
  if(amount>~(vxl_u64)0-f.constant_bytes)return -EOVERFLOW;
  amount+=f.constant_bytes;
  if(f.relative_arg_offset!=VXL_V2_ABSENT)relative=get32(b,f.relative_arg_offset);
  if(!amount || relative>ref->length || amount>ref->length-relative)return -ERANGE;
  gpu=(vxl_u64)o->gpu+ref->offset+relative;
  if(gpu>0xffffffffULL || amount-1>0xffffffffULL-gpu || (gpu & (f.alignment-1)))return -ERANGE;
  {vxl_u32 value=(vxl_u32)gpu;memcpy(b+f.arg_offset,&value,4);}
 }
 return 0;
}

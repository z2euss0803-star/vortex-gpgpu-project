#include <vxl/mgmt_timing.h>
#ifdef __KERNEL__
#include <linux/kernel.h>
#else
#include <stdio.h>
#endif
/* Live diagnostic storage may be recycled only after a successful complete
 * snapshot, never after a fault. The CLI persists snapshots before close. */
int vxl_mgmt_new_epoch(struct vxl_mgmt_trace *t){
 unsigned i;if(!t||!t->rows)return 0;
 if(t->active||t->used!=t->exported_used)return -EBUSY;
 if(t->flags||t->dropped)return -EIO;
 for(i=0;i<t->used;i++)if(t->rows[i].result||t->rows[i].flags)return -EIO;
 t->used=t->exported_used=0;return 0;
}
struct vxl_mgmt_row *vxl_mgmt_begin(struct vxl_mgmt_trace *t,const struct vxl_mgmt_key *key,vxl_u64 now){
 struct vxl_mgmt_row *r;
 if(!t||!t->rows||!key)return NULL;
 if(t->active){
  t->flags|=VXL_MGMT_BUSY;
  if(t->active->key.owner_cookie!=key->owner_cookie||t->active->key.generation!=key->generation)t->flags|=VXL_MGMT_STALE;
  return NULL;
 }
 if(t->used>=t->capacity){t->flags|=VXL_MGMT_FULL;if(t->dropped==~(vxl_u64)0)t->flags|=VXL_MGMT_OVERFLOW;else t->dropped++;return NULL;}
 r=&t->rows[t->used++];memset(r,0,sizeof(*r));r->key=*key;r->start_ns=r->last_end_ns=now;r->flags=VXL_MGMT_INCOMPLETE;t->active=r;return r;
}
static int accumulate(struct vxl_mgmt_row *r,vxl_u64 *ns,vxl_u64 *bytes,vxl_u64 *calls,vxl_u64 begin,vxl_u64 end,vxl_u64 count){
 vxl_u64 delta;
 if(end<begin||begin<r->start_ns){r->flags|=VXL_MGMT_BACKWARDS;return -EINVAL;}
 delta=end-begin;
 if(*ns>~(vxl_u64)0-delta||*bytes>~(vxl_u64)0-count||*calls==~(vxl_u64)0){r->flags|=VXL_MGMT_OVERFLOW;return -EOVERFLOW;}
 *ns+=delta;*bytes+=count;(*calls)++;return 0;
}
int vxl_mgmt_add(struct vxl_mgmt_row *r,enum vxl_mgmt_field f,vxl_u64 begin,vxl_u64 end,vxl_u64 bytes){
 int rc;if(!r)return 0;
 if((unsigned)f>=VXL_MGMT_FIELD_COUNT||!(r->flags&VXL_MGMT_INCOMPLETE))return -EINVAL;
 if(begin<r->last_end_ns){r->flags|=VXL_MGMT_BUSY;return -EINVAL;}
 rc=accumulate(r,&r->ns[f],&r->bytes[f],&r->calls[f],begin,end,bytes);if(!rc)r->last_end_ns=end;return rc;
}
int vxl_mgmt_nested_add(struct vxl_mgmt_row *r,enum vxl_mgmt_nested_field f,vxl_u64 begin,vxl_u64 end,vxl_u64 bytes){
 if(!r)return 0;
 if((unsigned)f>=VXL_MGMT_NESTED_COUNT||!(r->flags&VXL_MGMT_INCOMPLETE))return -EINVAL;
 return accumulate(r,&r->nested_ns[f],&r->nested_bytes[f],&r->nested_calls[f],begin,end,bytes);
}
void vxl_mgmt_finish(struct vxl_mgmt_trace *t,vxl_u64 now,int result){
 struct vxl_mgmt_row *r;if(!t||!t->active)return;r=t->active;
 r->end_ns=now;r->result=result;r->flags&=~VXL_MGMT_INCOMPLETE;
 if(now<r->last_end_ns||now<r->start_ns)r->flags|=VXL_MGMT_BACKWARDS;
 else r->total_ns=now-r->start_ns;
 t->flags|=r->flags;t->active=NULL;
}
static int emit_text(vxl_mgmt_emit_fn emit,void *ctx,const char *p){return emit(ctx,p,strlen(p));}
static int emit_values(vxl_mgmt_emit_fn emit,void *ctx,vxl_u64 a,vxl_u64 b,vxl_u64 c){
 char line[80];int n=snprintf(line,sizeof(line),",%llu,%llu,%llu",(unsigned long long)a,(unsigned long long)b,(unsigned long long)c);
 if(n<0||(size_t)n>=sizeof(line))return -EOVERFLOW;
 return emit(ctx,line,(size_t)n);
}
int vxl_mgmt_emit(const struct vxl_mgmt_trace *t,vxl_mgmt_emit_fn emit,void *ctx){
 char line[256];unsigned i,j;int n,rc;
 if(!t||!emit||t->used>t->capacity||(!t->rows&&t->used))return -EINVAL;
 if(t->active)return -EBUSY;
 n=snprintf(line,sizeof(line),"#VXL_MGMT_TIMING_V1 capacity=%u used=%u flags=%u dropped=%llu active=0\n",t->capacity,t->used,t->flags,(unsigned long long)t->dropped);
 if(n<0||(size_t)n>=sizeof(line))return -EOVERFLOW;
 rc=emit(ctx,line,(size_t)n);if(rc)return rc;
 rc=emit_text(emit,ctx,"row,owner_cookie,generation,token,command,start_ns,end_ns,total_ns,result,flags");if(rc)return rc;
#define MGMT_HEADER(upper,lower) rc=emit_text(emit,ctx,"," #lower "_ns," #lower "_bytes," #lower "_calls");if(rc)return rc;
 VXL_MGMT_FIELDS(MGMT_HEADER)
#undef MGMT_HEADER
#define NESTED_HEADER(upper,lower) rc=emit_text(emit,ctx,",nested_" #lower "_ns,nested_" #lower "_bytes,nested_" #lower "_calls");if(rc)return rc;
 VXL_MGMT_NESTED(NESTED_HEADER)
#undef NESTED_HEADER
 rc=emit_text(emit,ctx,"\n");if(rc)return rc;
 for(i=0;i<t->used;i++){
  const struct vxl_mgmt_row *r=&t->rows[i];
  n=snprintf(line,sizeof(line),"%u,%llu,%u,%llu,%u,%llu,%llu,%llu,%d,%u",i,(unsigned long long)r->key.owner_cookie,r->key.generation,(unsigned long long)r->key.token,r->key.command,(unsigned long long)r->start_ns,(unsigned long long)r->end_ns,(unsigned long long)r->total_ns,r->result,r->flags);
  if(n<0||(size_t)n>=sizeof(line))return -EOVERFLOW;
  rc=emit(ctx,line,(size_t)n);if(rc)return rc;
  for(j=0;j<VXL_MGMT_FIELD_COUNT;j++){rc=emit_values(emit,ctx,r->ns[j],r->bytes[j],r->calls[j]);if(rc)return rc;}
  for(j=0;j<VXL_MGMT_NESTED_COUNT;j++){rc=emit_values(emit,ctx,r->nested_ns[j],r->nested_bytes[j],r->nested_calls[j]);if(rc)return rc;}
  rc=emit_text(emit,ctx,"\n");if(rc)return rc;
 }
 return 0;
}

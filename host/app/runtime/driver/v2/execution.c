#include <vxl/v2/execution.h>
#include <vxl/hunet.h>
#ifdef __KERNEL__
#include <linux/slab.h>
#define scratch_alloc() kmalloc(4096,GFP_KERNEL)
#define scratch_free(p) kfree(p)
#else
#include <stdlib.h>
#define scratch_alloc() malloc(4096)
#define scratch_free(p) free(p)
#endif
/* Match the actual native PL bounce capacity; never put this on kernel stack. */
#define EXECUTION_BLOCK 4096u
static int zeros(const void *p,vxl_u32 n){const vxl_u8 *b=p;for(vxl_u32 i=0;i<n;i++)if(b[i])return 0;return 1;}
int vxl_v2_execution_bind(struct vxl_v2_execution *x,struct vxl_v2_hw *h,const struct vxl_v2_catalog *c,const struct vxl_v2_execution_policy *p,vxl_u32 n){
 struct vxl_v2_catalog_entry e;struct vxl_v2_schema_header schema;int rc;
 if(!x || !h || !h->ctx || !c || !p || !n || n>16 || n!=c->count || sizeof(*p)!=256)return -EINVAL;
 if(h->ctx->active || h->ctx->quarantined)return -EBUSY;
 for(vxl_u32 i=0;i<n;i++){
  vxl_u32 stack_guard=0;
  rc=vxl_v2_catalog_lookup(c,p[i].entry_id,&e);if(rc)return rc;
  rc=h->identity.provider.flags==1 || h->identity.provider.flags==4?vxl_v2_s8_admit(&h->identity,&e):vxl_v2_p8_admit(&h->identity,&e);if(rc)return rc;
  for(vxl_u32 k=0;k<i;k++)if(!memcmp(p[i].entry_id,p[k].entry_id,32))return -EEXIST;
  memcpy(&schema,c->bytes+e.schema_offset,64);
  if(!p[i].watchdog || p[i].reserved[0]>VXL_V2_RETIRE_GLOBAL_DRAIN || !zeros(p[i].reserved+1,sizeof(p[i].reserved)-4))return -EINVAL;
  if(p[i].reserved[0]==VXL_V2_RETIRE_GLOBAL_DRAIN){
   /* No fake CRT, CPU-injected markers or new stores under an old code ID. */
   if(schema.crt_bytes || schema.stack_bytes || p[i].retire_ref!=VXL_V2_ABSENT || p[i].retire_offset || p[i].retire_stride || p[i].retire_value || p[i].retire_count)return -EINVAL;
  }else{
   if(p[i].retire_count!=4 || p[i].retire_stride<4 || p[i].retire_stride>4096 || (p[i].retire_stride&3) || (p[i].retire_offset&3))return -EINVAL;
   if(p[i].retire_ref==VXL_V2_ABSENT){if(!schema.crt_bytes || (vxl_u64)p[i].retire_offset+3*p[i].retire_stride+4>schema.crt_bytes)return -ERANGE;}
   else if(p[i].retire_ref>=8)return -EINVAL;
  }
  if(p[i].guard_count>4)return -EINVAL;
  for(vxl_u32 k=0;k<4;k++){
   const struct vxl_v2_private_guard *g=&p[i].guards[k];vxl_u64 cap;
   if(k>=p[i].guard_count){if(!zeros(g,sizeof(*g)))return -EINVAL;continue;}
   if(!g->bytes || g->bytes>4096 || (g->bytes&63) || (g->offset&63) || !g->count || g->count>64 || g->stride<g->bytes || (g->stride&63))return -EINVAL;
   if(g->gpu==schema.stack_gpu && schema.stack_bytes){cap=schema.stack_bytes;if(!g->offset && g->bytes>=64)stack_guard=1;}
   else if(g->gpu==schema.crt_gpu && schema.crt_bytes)cap=schema.crt_bytes;
   else return -ERANGE;
   if(vxl_range_check(cap,g->offset,(vxl_u64)(g->count-1)*g->stride+g->bytes))return -ERANGE;
   for(vxl_u32 repeat=0;repeat<g->count;repeat++){
    vxl_u64 at=(vxl_u64)g->offset+repeat*(vxl_u64)g->stride;
    if(g->gpu==schema.crt_gpu && p[i].retire_ref==VXL_V2_ABSENT)for(vxl_u32 core=0;core<4;core++){vxl_u64 off=p[i].retire_offset+core*p[i].retire_stride;if(off<at+g->bytes && at<off+4)return -EINVAL;}
    for(vxl_u32 prev=0;prev<k;prev++){const struct vxl_v2_private_guard *b=&p[i].guards[prev];if(g->gpu==b->gpu)for(vxl_u32 br=0;br<b->count;br++){vxl_u64 bo=(vxl_u64)b->offset+br*(vxl_u64)b->stride;if(at<bo+b->bytes && bo<at+g->bytes)return -EINVAL;}}
   }
  }
  if(schema.stack_bytes && !stack_guard)return -EINVAL;
  if(p[i].tex.enabled>1 || p[i].tex.bilinear>1 || (p[i].tex.enabled && (p[i].tex.ref_index>=8 || !p[i].tex.width || p[i].tex.width>2048 || !p[i].tex.height || p[i].tex.height>2048 || p[i].tex.pitch<p[i].tex.width*4 || p[i].tex.pitch>8192 || (p[i].tex.pitch&3))))return -EINVAL;
  if(!p[i].tex.enabled && !zeros(&p[i].tex,sizeof(p[i].tex)))return -EINVAL;
  /* P8 raster tables require explicit indirect bounds. Transform programs do not. */
  if(e.namespace_id==2 && (e.logical_program==10 || e.logical_program==12) && p[i].directory_ref==VXL_V2_ABSENT)return -EINVAL;
  if(p[i].directory_ref!=VXL_V2_ABSENT){if(p[i].directory_ref>=8 || p[i].indices_ref>=8 || p[i].directory_ref==p[i].indices_ref || (p[i].faces_arg_offset&3) || p[i].faces_arg_offset>e.args_bytes-4 || !p[i].tile_count || p[i].tile_count>16384 || !p[i].max_faces || p[i].max_faces>30926 || !p[i].max_indices || p[i].max_indices>262144)return -EINVAL;}
  else if(p[i].indices_ref!=VXL_V2_ABSENT || p[i].faces_arg_offset || p[i].tile_count || p[i].max_faces || p[i].max_indices)return -EINVAL;
 }
 memset(x,0,sizeof(*x));x->hw=h;x->count=n;memcpy(x->policies,p,n*sizeof(*p));return 0;
}
static vxl_u64 now_execution(void *v){struct vxl_v2_execution *x=v;return x->hw->now_ns(x->hw->os);}
static int rw(struct vxl_v2_execution *x,const struct vxl_v2_ref *r,vxl_u64 off,void *b,vxl_u64 n,int write){
 struct vxl_v2_ctx *c=x->hw->ctx;struct vxl_v2_object *o;int rc;
 if(!n || vxl_range_check(r->length,off,n))return -ERANGE;
 rc=vxl_v2_ref_resolve(c,c->owner,r,1,&o);if(rc)return rc;
 /* Trusted parameter/CRT initialization only. Public sealed/pinned COPY is
  * still forbidden. Every caller here holds the device's single mutex. */
 return c->ops->copy(c->memory_context,o,r->offset+off,b,n,write);
}
static int hash_scratch(struct vxl_v2_execution *x,const struct vxl_v2_ref *r,vxl_u8 out[32],vxl_u8 *bytes){
 struct vxl_hunet_sha sha;vxl_u64 off=0,n,begin=now_execution(x);int rc=0;
 vxl_hunet_sha_init(&sha);
 while(off<r->length){n=r->length-off;if(n>EXECUTION_BLOCK)n=EXECUTION_BLOCK;rc=rw(x,r,off,bytes,n,0);if(rc)break;vxl_hunet_sha_update(&sha,bytes,n);off+=n;}
 if(!rc)vxl_hunet_sha_final(&sha,out);
 x->last.digest_ns+=now_execution(x)-begin;return rc;
}
static int hash(struct vxl_v2_execution *x,const struct vxl_v2_ref *r,vxl_u8 out[32]){
 vxl_u8 *bytes=scratch_alloc();int rc;if(!bytes)return -ENOMEM;
 rc=hash_scratch(x,r,out,bytes);scratch_free(bytes);return rc;
}
static int same_hash(struct vxl_v2_execution *x,const struct vxl_v2_ref *r,const vxl_u8 expected[32]){vxl_u8 got[32];int rc=hash(x,r,got);return rc?rc:memcmp(got,expected,32)?-EBADMSG:0;}
static int fast_mode(const struct vxl_v2_execution *x){return x->job && (x->job->flags&VXL_V2_JOB_FAST_RUNTIME);}
/* Metadata only. No device content is read, no digest is reused, and no source
 * or args freshness is asserted. Validate every owned/data/staged-args ref. */
static int metadata(struct vxl_v2_execution *x){
 struct vxl_v2_ctx *c=x->hw->ctx;struct vxl_v2_object *o;const struct vxl_v2_job *j=x->job;struct vxl_v2_ref staged;vxl_u32 domain=0;int rc;
 if(!j || j->num_refs>8 || !x->program || !x->program->owned_count || x->program->owned_count>5)return -EINVAL;
 if(j->program_handle!=x->program->ref.handle || j->program_generation!=x->program->ref.generation || j->device_generation!=x->program->ref.device_generation)return -ESTALE;
 for(vxl_u32 i=0;i<x->program->owned_count;i++){rc=vxl_v2_ref_resolve(c,c->owner,&x->program->owned[i],1,&o);if(rc)return rc;if(!o->pins[2])return -EPERM;}
 for(vxl_u32 i=0;i<j->num_refs;i++){rc=vxl_v2_ref_resolve(c,c->owner,&j->refs[i],j->refs[i].access,&o);if(rc)return rc;if(!o->pins[2] || (o->purpose!=VXL_V2_DATA && o->purpose!=VXL_V2_READONLY))return -EPERM;}
 for(vxl_u32 i=0;i<64;i++)if(c->objects[i].handle==j->args_handle){domain=c->objects[i].domain;break;}
 staged=(struct vxl_v2_ref){j->args_handle,j->args_generation,j->args_offset,j->args_bytes,1,domain,j->device_generation};
 rc=vxl_v2_ref_resolve(c,c->owner,&staged,1,&o);if(rc)return rc;
 return o->purpose==VXL_V2_ARGS && o->pins[2]?0:-EPERM;
}
static int guards(struct vxl_v2_execution *x){
 struct vxl_v2_ctx *c=x->hw->ctx;vxl_u64 begin=now_execution(x);int rc=0;
 for(vxl_u32 i=0;i<64;i++)if(c->objects[i].handle && c->objects[i].guard_bytes){rc=vxl_v2_native_guards(x->hw,&c->objects[i],0);if(rc)break;}
 x->last.guard_ns+=now_execution(x)-begin;return rc;
}
static int sources(struct vxl_v2_execution *x,int initial){
 /* Local to this invocation only: every prepare/DONE/pre-ACK phase rereads.
  * Resolve EVERY slot before reuse, and compare its own expected digest. */
 struct source_digest {struct vxl_v2_ref ref;vxl_u64 owner,epoch;vxl_u32 access;vxl_u8 sha[32];} cache[8];
 struct vxl_v2_ctx *c=x->hw->ctx;const struct vxl_v2_program *p=x->program;
 vxl_u8 *bytes=scratch_alloc(),got[32];vxl_u32 count=0;int rc;if(!bytes)return -ENOMEM;
 rc=hash_scratch(x,&p->owned[0],got,bytes);if(!rc && memcmp(got,p->entry.code,32))rc=-EBADMSG;if(rc)goto done;
 if(p->entry.rodata_bytes){rc=hash_scratch(x,&p->owned[1],got,bytes);if(!rc && memcmp(got,p->entry.rodata,32))rc=-EBADMSG;if(rc)goto done;}
 for(vxl_u32 i=0;i<x->job->num_refs;i++)if(!(x->job->refs[i].access&2)){
  const struct vxl_v2_ref *r=&x->job->refs[i];struct vxl_v2_object *o;vxl_u32 k;
  rc=vxl_v2_ref_resolve(c,c->owner,r,1,&o);if(rc)goto done;
  for(k=0;k<count;k++){
   const struct source_digest *d=&cache[k];const struct vxl_v2_ref *a=&d->ref;
   if(d->owner==o->owner && d->epoch==o->epoch && d->access==o->access && a->handle==r->handle && a->object_generation==r->object_generation && a->device_generation==r->device_generation && a->domain==r->domain && a->access==r->access && a->offset==r->offset && a->length==r->length)break;
  }
  if(k==count){
   rc=hash_scratch(x,r,got,bytes);if(rc)goto done;
   cache[k].ref=*r;cache[k].owner=o->owner;cache[k].epoch=o->epoch;cache[k].access=o->access;memcpy(cache[k].sha,got,32);count++;
  }
  if(initial)memcpy(x->input_sha[i],cache[k].sha,32);
  else if(memcmp(cache[k].sha,x->input_sha[i],32)){rc=-EBADMSG;goto done;}
 }
 rc=0;
done:scratch_free(bytes);return rc;
}
static int indirect(struct vxl_v2_execution *x){
 const struct vxl_v2_execution_policy *p=&x->policy;const struct vxl_v2_job *j=x->job;vxl_u32 faces,pair[2],index,next=0;vxl_u8 *bytes;int rc;
 if(p->directory_ref==VXL_V2_ABSENT)return 0;
 if(p->directory_ref>=j->num_refs || p->indices_ref>=j->num_refs || (j->refs[p->directory_ref].access&2) || (j->refs[p->indices_ref].access&2))return -EACCES;
 memcpy(&faces,x->shadow+p->faces_arg_offset,4);if(!faces || faces>p->max_faces || (vxl_u64)p->tile_count*8>j->refs[p->directory_ref].length)return -ERANGE;
 bytes=scratch_alloc();if(!bytes)return -ENOMEM;
 /* Real PS/CDMA block reads; validate each pair/index including the tail. */
 for(vxl_u32 tile=0;tile<p->tile_count;){
  vxl_u32 count=p->tile_count-tile;if(count>EXECUTION_BLOCK/8)count=EXECUTION_BLOCK/8;
  rc=rw(x,&j->refs[p->directory_ref],(vxl_u64)tile*8,bytes,(vxl_u64)count*8,0);if(rc)goto done;
  for(vxl_u32 k=0;k<count;k++){
   memcpy(pair,bytes+k*8,8);
   if(pair[0]!=next || pair[1]>p->max_indices-next){rc=-ERANGE;goto done;}
   next+=pair[1];
  }
  tile+=count;
 }
 if((vxl_u64)next*4>j->refs[p->indices_ref].length){rc=-ERANGE;goto done;}
 for(vxl_u32 at=0;at<next;){
  vxl_u32 count=next-at;if(count>EXECUTION_BLOCK/4)count=EXECUTION_BLOCK/4;
  rc=rw(x,&j->refs[p->indices_ref],(vxl_u64)at*4,bytes,(vxl_u64)count*4,0);if(rc)goto done;
  for(vxl_u32 k=0;k<count;k++){memcpy(&index,bytes+k*4,4);if(index>=faces){rc=-ERANGE;goto done;}}
  at+=count;
 }
 rc=0;
done:scratch_free(bytes);return rc;
}
static int choose_private(struct vxl_v2_execution *x,vxl_u32 gpu,struct vxl_v2_ref *out){
 struct vxl_v2_ctx *c=x->hw->ctx;struct vxl_v2_object *o;
 for(vxl_u32 i=0;i<x->program->owned_count;i++){int rc=vxl_v2_ref_resolve(c,c->owner,&x->program->owned[i],1,&o);if(rc)return rc;if(o->gpu==gpu){*out=x->program->owned[i];return 0;}}
 return -ERANGE;
}
static int private_guards(struct vxl_v2_execution *x,int initialize){
 vxl_u8 bytes[64];vxl_u64 begin=now_execution(x);int rc=0;
 for(vxl_u32 i=0;i<x->policy.guard_count;i++){
  const struct vxl_v2_private_guard *g=&x->policy.guards[i];struct vxl_v2_ref ref;
  rc=choose_private(x,g->gpu,&ref);if(rc)break;
  for(vxl_u32 repeat=0;repeat<g->count && !rc;repeat++)for(vxl_u32 off=0;off<g->bytes;off+=64){
   if(initialize)memset(bytes,0xa7,64);
   rc=rw(x,&ref,(vxl_u64)g->offset+repeat*(vxl_u64)g->stride+off,bytes,64,initialize);if(rc)break;
   if(!initialize)for(vxl_u32 k=0;k<64;k++)if(bytes[k]!=0xa7){rc=-EBADMSG;break;}
   if(rc)break;
  }
  if(rc)break;
 }
 x->last.guard_ns+=now_execution(x)-begin;return rc;
}
static int prepare_execution(void *v,const struct vxl_v2_program *p,const struct vxl_v2_job *j,const void *a,vxl_u32 n){
 struct vxl_v2_execution *x=v;struct vxl_v2_schema_header schema;vxl_u32 sentinel;int rc;vxl_u64 begin;
 if(!x || !x->hw || !x->hw->now_ns || !p || !j || !a || !n || n>4096 || n!=p->entry.args_bytes)return -EINVAL;
 if(x->prepared || x->started)return -EBUSY;
 memset(&x->last,0,sizeof(x->last));x->program=p;x->job=j;x->dma_begin=x->hw->cdma_ns;x->bytes_begin=x->hw->io_bytes;
 rc=vxl_v2_hw_idle(x->hw);if(rc)return rc;
 for(vxl_u32 i=0;i<x->count;i++)if(!memcmp(x->policies[i].entry_id,p->ref.entry_id,32)){x->policy=x->policies[i];goto found;}
 return -EKEYREJECTED;
found:
 memcpy(&schema,x->hw->ctx->programs->catalog.bytes+p->entry.schema_offset,64);
 rc=choose_private(x,p->entry.args_gpu,&x->args);if(rc)return rc;
 if(x->policy.reserved[0]==VXL_V2_RETIRE_EXIT_WORDS){
  if(x->policy.retire_ref==VXL_V2_ABSENT){rc=choose_private(x,schema.crt_gpu,&x->retire);if(rc)return rc;}
  else {if(x->policy.retire_ref>=j->num_refs || !(j->refs[x->policy.retire_ref].access&2))return -EACCES;x->retire=j->refs[x->policy.retire_ref];}
  if((vxl_u64)x->policy.retire_offset+3*x->policy.retire_stride+4>x->retire.length)return -ERANGE;
 }
 /* Read-only inputs may not alias any write destination. */
 for(vxl_u32 i=0;i<j->num_refs;i++)for(vxl_u32 k=0;k<j->num_refs;k++)if(!(j->refs[i].access&2) && (j->refs[k].access&2) && j->refs[i].handle==j->refs[k].handle && j->refs[i].offset<j->refs[k].offset+j->refs[k].length && j->refs[k].offset<j->refs[i].offset+j->refs[i].length)return -EINVAL;
 rc=guards(x);if(rc)return rc;x->args_bytes=n;memcpy(x->shadow,a,n);
 rc=fast_mode(x)?metadata(x):sources(x,1);if(rc)return rc;rc=indirect(x);if(rc)return rc;
 begin=now_execution(x);rc=rw(x,&x->args,0,x->shadow,n,1);if(rc)return rc;
 sentinel=~x->policy.retire_value;
 if(x->policy.reserved[0]==VXL_V2_RETIRE_EXIT_WORDS)for(vxl_u32 core=0;core<4;core++){rc=rw(x,&x->retire,x->policy.retire_offset+core*x->policy.retire_stride,&sentinel,4,1);if(rc)return rc;}
 rc=private_guards(x,1);if(rc)return rc;
 /* Each program's TEX state is private trusted policy, never inherited from
  * the previous job. The frozen P8 manual sampler binds a disabled policy. */
 rc=vxl_v2_prepare_tex(x->hw,j,&x->policy.tex);if(rc)return rc;
 x->hw->write(x->hw->os,0x10,p->entry.entry_pc);x->hw->write(x->hw->os,0x14,p->entry.args_gpu);x->hw->write(x->hw->os,0x18,x->policy.watchdog);x->hw->memory_barrier(x->hw->os,1);
 x->last.handoff_ns+=now_execution(x)-begin;x->prepared=1;return 0;
}
static int registers(struct vxl_v2_execution *x){
 static const vxl_u32 offsets[6]={0x40,0x44,0x48,0x4c,0x54,0x50};struct vxl_v2_hw *h=x->hw;
 if(h->read(h->os,0x10)!=x->program->entry.entry_pc || h->read(h->os,0x14)!=x->program->entry.args_gpu || h->read(h->os,0x18)!=x->policy.watchdog)return -EBADMSG;
 for(vxl_u32 i=0;i<6;i++)if(h->read(h->os,offsets[i])!=h->tex_state[i])return -EBADMSG;
 return 0;
}
static int parameters(struct vxl_v2_execution *x){vxl_u8 expected[32];vxl_hunet_hash(x->shadow,x->args_bytes,expected);return same_hash(x,&x->args,expected);}
static int start_execution(void *v,const struct vxl_v2_program *p,const struct vxl_v2_job *j,struct vxl_v2_start_proof *s){
 struct vxl_v2_execution *x=v;int rc;vxl_u64 begin=now_execution(x);
 if(!s || !x->prepared || x->started || x->program!=p || x->job!=j)return -EINVAL;
 rc=vxl_v2_hw_idle(x->hw);if(rc)return rc;rc=registers(x);if(rc)return rc;rc=fast_mode(x)?metadata(x):parameters(x);if(rc)return rc;
 x->jobs_before=x->hw->read(x->hw->os,0x2c);x->hw->memory_barrier(x->hw->os,1);
 s->start_ns=now_execution(x);s->jobs_before=x->jobs_before;s->watchdog=x->policy.watchdog;
 /* From this store onward hardware may have started even if the next read
  * faults. The core retains every pin and never retries this START. */
 x->started=1;s->started=1;x->hw->write(x->hw->os,8,1);x->last.handoff_ns+=now_execution(x)-begin;return 0;
}
int vxl_v2_execution_snapshot(struct vxl_v2_execution *x,vxl_u64 seq,struct vxl_v2_snapshot *s){
 struct vxl_v2_hw *h;vxl_u64 begin;vxl_u32 sr,value;int rc=0;
 if(!x || !s || !(h=x->hw) || !h->locked || !h->locked(h->os) || !h->now_ns)return -EDEADLK;
 begin=now_execution(x);x->last.sequence=seq;x->last.now_ns=begin;
 /* Capture MMIO FIRST, even after a DMA/integrity failure. No ACK/reset here. */
 x->last.status=h->read(h->os,0xc);x->last.errors=h->read(h->os,0x1c);x->last.pending_w=h->read(h->os,0x20);x->last.pending_r=h->read(h->os,0x24);x->last.jobs=h->read(h->os,0x2c);x->last.elapsed_raw=h->read(h->os,0x28);x->last.watchdog=h->read(h->os,0x18);x->last.done_seen=!!(x->last.status&2);sr=h->cdma.read(h->cdma.context,4);
 x->cdma_status=sr;x->last.cdma_idle=(sr&2) && !(sr&0x4770) && (!h->dma_completed || (sr&0x1000)) && !(h->read(h->os,0x58)&1);
 x->verified=0;x->last.parameters_ok=0;x->last.source_ok=0;x->last.metadata_ok=0;x->last.guard_ok=0;x->last.core_mask=0;x->last.exitcode=0;
 if(x->started && x->last.done_seen && !(x->last.status&4) && !x->last.errors && !x->last.pending_w && !x->last.pending_r && x->last.jobs==(vxl_u32)(x->jobs_before+1) && x->last.cdma_idle && !h->ctx->quarantined){
  x->last.parameters_ok=0;x->last.source_ok=0;x->last.guard_ok=0;x->last.core_mask=0;x->last.exitcode=0;
  rc=registers(x);
  if(fast_mode(x)){if(!rc)rc=metadata(x);if(!rc)x->last.metadata_ok=1;}
  else{if(!rc)rc=parameters(x);if(!rc)x->last.parameters_ok=1;if(!rc)rc=sources(x,0);if(!rc)x->last.source_ok=1;}
  if(!rc)rc=guards(x);
  if(!rc)rc=private_guards(x,0);
  if(!rc)x->last.guard_ok=1;
  if(!rc && x->policy.reserved[0]==VXL_V2_RETIRE_EXIT_WORDS)for(vxl_u32 core=0;core<4;core++){rc=rw(x,&x->retire,x->policy.retire_offset+core*x->policy.retire_stride,&value,4,0);if(rc)break;x->retire_words[core]=value;if(value==x->policy.retire_value)x->last.core_mask|=1u<<core;else x->last.exitcode=value?value:1;}
  else if(!rc){
   /* This is inferred global retirement, explicitly selected by hashed boot
    * policy. Never label it as a read of four program exit/retirement words. */
   if(x->last.status!=0x502u)rc=-EBADMSG;
   else x->last.core_mask=15;
  }
  x->last.cycles_valid=!rc && x->last.elapsed_raw && x->last.elapsed_raw<x->policy.watchdog && x->last.watchdog==x->policy.watchdog;
  x->verified=!rc && x->last.guard_ok && (fast_mode(x)?x->last.metadata_ok:(x->last.parameters_ok && x->last.source_ok)) && x->last.core_mask==15 && !x->last.exitcode && x->last.cycles_valid;
  /* Integrity failures are represented by snapshot flags, preserving the raw
   * registers through the common completion path. IO failure also returns rc. */
  if(rc==-EBADMSG)rc=0;
 }
 x->last.poll_ns+=now_execution(x)-begin;x->last.cdma_ns=h->cdma_ns-x->dma_begin;x->last.io_bytes=h->io_bytes-x->bytes_begin;*s=x->last;return rc;
}
static int ack_execution(void *v,vxl_u32 *idle,vxl_u32 *ready){
 struct vxl_v2_execution *x=v;struct vxl_v2_snapshot final;struct vxl_v2_hw *h=x->hw;vxl_u64 begin=now_execution(x);int rc;
 if(!idle || !ready || !x->started || !x->verified)return -EPERM;
 /* Recheck the retirement snapshot under the same lock immediately before
  * ACK. DONE must remain stable; there is no reset fallback. */
 rc=vxl_v2_execution_snapshot(x,x->last.sequence,&final);if(rc || !x->verified || !(final.status&2) || final.pending_w || final.pending_r || final.errors || (final.status&4) || !final.cdma_idle || final.jobs!=(vxl_u32)(x->jobs_before+1))return rc?rc:-EIO;
 h->memory_barrier(h->os,1);h->write(h->os,8,2);h->memory_barrier(h->os,0);
 rc=vxl_v2_hw_idle(h);*idle=!rc;*ready=h->ready(h->os)==1 && (h->read(h->os,0x34)&1);
 x->last.handoff_ns+=now_execution(x)-begin;if(rc || !*ready)return rc?rc:-EIO;
 x->prepared=x->started=x->verified=0;return 0;
}
const struct vxl_v2_execution_ops vxl_v2_native_execution_ops={prepare_execution,start_execution,ack_execution,now_execution};

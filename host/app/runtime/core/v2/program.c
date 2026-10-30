#include <vxl/v2/program.h>
#include <vxl/hunet.h>
int vxl_v2_table_bind(struct vxl_v2_ctx *c,struct vxl_v2_program_table *t,const struct vxl_v2_catalog *cat){
 struct vxl_v2_catalog checked;int rc;unsigned int i;
 if(!c || !t || !cat)return -EINVAL;
 if(c->owner || c->live || c->active || c->quarantined)return -EBUSY;
 if(c->programs)for(i=0;i<16;i++)if(c->programs->programs[i].ref.handle)return -EBUSY;
 rc=vxl_v2_catalog_open(&checked,cat->bytes,cat->length,&cat->provider);if(rc)return rc;
 if(memcmp(checked.sha,cat->sha,32))return -EBADMSG;
 memset(t,0,sizeof(*t));t->catalog=checked;c->programs=t;return 0;
}
/* Fixed addresses only originate from the admitted, hashed binary schema. No ioctl
 * exposes this allocator. CRT/stack may occupy their declared PS reservations. */
static int reserve_owned(struct vxl_v2_ctx *c,vxl_u32 gpu,vxl_u32 length,vxl_u32 purpose,struct vxl_v2_ref *out){
 struct vxl_v2_object o;vxl_u64 off,n,base,cap;vxl_u32 domain;unsigned int i,slot=64;int rc;
 if(!length || (gpu&63))return -EINVAL;
 n=((vxl_u64)length+63)&~63ULL;
 if(gpu>=c->profile.ps_base && (vxl_u64)gpu-c->profile.ps_base<c->profile.ps_size){domain=VXL_PS;base=c->profile.ps_base;cap=c->profile.ps_size;off=gpu-base;}
 else if(gpu>=0x80000000u){domain=VXL_PL;base=c->profile.pl_base;cap=c->profile.pl_size;off=(vxl_u64)gpu-0x80000000ULL;}
 else return -ERANGE;
 rc=vxl_range_check(cap,off,n);if(rc)return rc;
 /* These are transport/fence reservations even for trusted private objects. */
 if(domain==VXL_PS && ((off<0x30040 && off+n>0x30000) || (off<VXL_V2_BOUNCE_OFFSET+4096 && off+n>VXL_V2_BOUNCE_OFFSET)))return -ERANGE;
 if(domain==VXL_PS && purpose!=VXL_V2_DATA && ((off<0x3000 && off+n>0x2000) || (off<0x100000 && off+n>0xff000)))return -ERANGE;
 for(i=0;i<64;i++){
  const struct vxl_v2_object *p=&c->objects[i];
  if(!p->handle){if(slot==64)slot=i;continue;}
  if(p->domain==domain && off<p->offset+p->allocated && p->offset<off+n)return -EBUSY;
 }
 if(slot==64)return -ENOSPC;
 if(c->serial==~(vxl_u64)0 || base>~(vxl_u64)0-off)return -EOVERFLOW;
 memset(&o,0,sizeof(o));o.bus=base+off;rc=vxl_addr_encode(&c->profile,domain,o.bus,n,&o.gpu);if(rc)return rc;
 o.handle=++c->serial;o.generation=o.handle;o.owner=c->owner;o.epoch=c->epoch;o.offset=off;o.length=length;o.allocated=n;o.domain=domain;o.purpose=purpose;o.access=3;
 if(!c->ops || !c->ops->allocate || !c->ops->copy || !c->ops->free)return -ENODEV;
 rc=c->ops->allocate(c->memory_context,&o);if(rc)return rc;
 c->objects[slot]=o;c->live++;*out=(struct vxl_v2_ref){o.handle,o.generation,0,length,3,domain,c->epoch};return 0;
}
static int upload_and_check(struct vxl_v2_ctx *c,vxl_u64 owner,struct vxl_v2_ref ref,const void *bytes,const vxl_u8 expected[32]){
 vxl_u8 chunk[256],sum[32];vxl_u64 off=0;struct vxl_hunet_sha sha;int rc;
 while(off<ref.length){struct vxl_v2_ref part=ref;part.offset=off;part.length=ref.length-off;if(part.length>256)part.length=256;
  rc=vxl_v2_buffer_copy(c,owner,&part,(void *)((const vxl_u8 *)bytes+off),1);if(rc)return rc;off+=part.length;}
 off=0;vxl_hunet_sha_init(&sha);
 while(off<ref.length){struct vxl_v2_ref part=ref;part.offset=off;part.length=ref.length-off;if(part.length>256)part.length=256;
  rc=vxl_v2_buffer_copy(c,owner,&part,chunk,0);if(rc)return rc;vxl_hunet_sha_update(&sha,chunk,part.length);off+=part.length;}
 vxl_hunet_sha_final(&sha,sum);if(memcmp(sum,expected,32)){c->quarantined=1;return -EBADMSG;}return 0;
}
static int drop_owned(struct vxl_v2_ctx *c,vxl_u64 owner,struct vxl_v2_program *p,unsigned int pinned){
 unsigned int i;int rc;
 for(i=0;i<pinned;i++){rc=vxl_v2_buffer_unpin(c,owner,&p->owned[i],2);if(rc)return rc;}
 for(i=0;i<p->owned_count;i++){rc=vxl_v2_buffer_release(c,owner,&p->owned[i]);if(rc){c->quarantined=1;return rc;}}
 return 0;
}
static int verify_resident(struct vxl_v2_ctx *c,vxl_u64 owner,const struct vxl_v2_ref *ref,const vxl_u8 expected[32]){
 struct vxl_v2_object *o;struct vxl_hunet_sha sha;vxl_u8 chunk[256],sum[32];vxl_u64 off=0,n;int rc=vxl_v2_ref_resolve(c,owner,ref,1,&o);if(rc)return rc;
 if(!c->ops || !c->ops->copy)return -ENODEV;
 /* Trusted read-only integrity operation under the device mutex. Permanent
  * program pins remain intact; the public COPY path stays unable to bypass them. */
 vxl_hunet_sha_init(&sha);
 while(off<ref->length){n=ref->length-off;if(n>256)n=256;rc=c->ops->copy(c->memory_context,o,ref->offset+off,chunk,n,0);if(rc){if(rc==-EIO || rc==-ETIMEDOUT)c->quarantined=1;return rc;}vxl_hunet_sha_update(&sha,chunk,n);off+=n;}
 vxl_hunet_sha_final(&sha,sum);if(memcmp(sum,expected,32)){c->quarantined=1;return -EBADMSG;}return 0;
}
static int initialize_zero(struct vxl_v2_ctx *c,vxl_u64 owner,struct vxl_v2_ref ref){
 vxl_u8 zeros[256]={0},readback[256];vxl_u64 off=0;int rc;
 while(off<ref.length){struct vxl_v2_ref part=ref;part.offset=off;part.length=ref.length-off;if(part.length>256)part.length=256;
  rc=vxl_v2_buffer_copy(c,owner,&part,zeros,1);if(rc)return rc;
  rc=vxl_v2_buffer_copy(c,owner,&part,readback,0);if(rc)return rc;
  if(memcmp(readback,zeros,(size_t)part.length)){c->quarantined=1;return -EBADMSG;}off+=part.length;}
 return 0;
}
int vxl_v2_program_load(struct vxl_v2_ctx *c,vxl_u64 owner,const struct vxl_v2_catalog_entry *entry,const void *code,const void *rodata,struct vxl_v2_program_ref *out){
 struct vxl_v2_program_table *t;struct vxl_v2_catalog_entry trusted;struct vxl_v2_schema_header s;struct vxl_v2_program p;vxl_u8 sha[32];unsigned int slot,i,pinned=0,first_private;int rc;
 if(!c || !entry || !out || !code)return -EINVAL;
 rc=vxl_v2_owner_check(c,owner,c->epoch);if(rc)return rc;
 if(c->active || !c->programs)return -EBUSY;
 t=c->programs;rc=vxl_v2_catalog_lookup(&t->catalog,entry->entry_id,&trusted);if(rc)return rc;
 if(memcmp(entry,&trusted,512))return -EKEYREJECTED;
 if(trusted.rodata_bytes && !rodata)return -EINVAL;
 vxl_hunet_hash(code,trusted.code_bytes,sha);if(memcmp(sha,trusted.code,32))return -EBADMSG;
 vxl_hunet_hash(rodata,trusted.rodata_bytes,sha);if(memcmp(sha,trusted.rodata,32))return -EBADMSG;
 for(i=0;i<16;i++)if(t->programs[i].ref.handle && !memcmp(t->programs[i].ref.entry_id,trusted.entry_id,32)){
  if(t->programs[i].users==~(vxl_u32)0)return -EOVERFLOW;
  rc=verify_resident(c,owner,&t->programs[i].owned[0],trusted.code);if(rc)return rc;
  if(trusted.rodata_bytes){rc=verify_resident(c,owner,&t->programs[i].owned[1],trusted.rodata);if(rc)return rc;}
  t->programs[i].users++;*out=t->programs[i].ref;return 0;
 }
 for(slot=0;slot<16;slot++)if(!t->programs[slot].ref.handle)break;
 if(slot==16)return -ENOSPC;
 if(t->serial==~(vxl_u64)0)return -EOVERFLOW;
 memset(&p,0,sizeof(p));p.entry=trusted;p.users=1;p.ref.handle=++t->serial;p.ref.generation=p.ref.handle;p.ref.device_generation=c->epoch;memcpy(p.ref.entry_id,trusted.entry_id,32);
 memcpy(&s,t->catalog.bytes+trusted.schema_offset,64);
 rc=reserve_owned(c,s.code_gpu,trusted.code_bytes,VXL_V2_CODE,&p.owned[p.owned_count]);if(rc)goto fail;p.owned_count++;
 rc=upload_and_check(c,owner,p.owned[0],code,trusted.code);if(rc)goto fail;
 if(trusted.rodata_bytes){rc=reserve_owned(c,s.rodata_gpu,trusted.rodata_bytes,VXL_V2_READONLY,&p.owned[p.owned_count]);if(rc)goto fail;p.owned_count++;rc=upload_and_check(c,owner,p.owned[p.owned_count-1],rodata,trusted.rodata);if(rc)goto fail;}
 first_private=p.owned_count;
 rc=reserve_owned(c,trusted.args_gpu,trusted.args_bytes,VXL_V2_ARGS,&p.owned[p.owned_count]);if(rc)goto fail;p.owned_count++;
 if(s.crt_bytes){rc=reserve_owned(c,s.crt_gpu,s.crt_bytes,VXL_V2_DATA,&p.owned[p.owned_count]);if(rc)goto fail;p.owned_count++;}
 if(s.stack_bytes){rc=reserve_owned(c,s.stack_gpu,s.stack_bytes,VXL_V2_DATA,&p.owned[p.owned_count]);if(rc)goto fail;p.owned_count++;}
 for(i=first_private;i<p.owned_count;i++){rc=initialize_zero(c,owner,p.owned[i]);if(rc)goto fail;}
 for(i=0;i<p.owned_count;i++){
  /* All CPU aliases become read-only; trusted GPU may update private CRT/stack.
   * Args installation is a separate checked provider operation, not public COPY. */
  rc=vxl_v2_buffer_seal(c,owner,&p.owned[i]);if(rc)goto fail;
  p.owned[i].access=1;rc=vxl_v2_buffer_pin(c,owner,&p.owned[i],2);if(rc)goto fail;pinned++;
 }
 t->programs[slot]=p;*out=p.ref;return 0;
fail:
 if(c->quarantined){p.pins=1;t->programs[slot]=p;return rc;}
 if(drop_owned(c,owner,&p,pinned))return -EIO;
 return rc;
}
int vxl_v2_program_release(struct vxl_v2_ctx *c,vxl_u64 owner,const struct vxl_v2_program_ref *ref){
 unsigned int i;int rc;
 if(!c || !ref || !c->programs)return -EINVAL;
 rc=vxl_v2_owner_check(c,owner,ref->device_generation);if(rc)return rc;
 for(i=0;i<16;i++){
  struct vxl_v2_program *p=&c->programs->programs[i];
  if(!ref->handle || p->ref.handle!=ref->handle)continue;
  if(p->ref.generation!=ref->generation || memcmp(p->ref.entry_id,ref->entry_id,32))return -ESTALE;
  if(p->pins || c->active)return -EBUSY;
  if(p->users>1){p->users--;return 0;}
  rc=drop_owned(c,owner,p,p->owned_count);if(rc)return rc;
  memset(p,0,sizeof(*p));return 0;
 }
 return -EBADF;
}

#include <vxl/s8.h>
#include <vxl/s8_trace.h>
#include <vxl/engine.h>
#include <vxl/hunet.h> /* portable SHA primitive; no HUNet context */
static int fail_io(struct vxl_s8_ctx *c,int rc) {
 if(rc==-EIO || rc==-ETIMEDOUT){c->engine->quarantined=1;c->engine->job.state=VXL_QUARANTINED;c->engine->job.pinned=1;c->engine->job.result=rc;}
 return rc;
}
int vxl_s8_io_bytes(struct vxl_s8_ctx *c,vxl_u64 address,void *buffer,vxl_u64 bytes,int write) {
 vxl_u8 *data=buffer,temp[16];vxl_u64 n,aligned;int rc;
 if(!c || !c->engine || !c->io.rw || !buffer || !bytes || bytes>~(vxl_u64)0-address)return -EINVAL;
 while(bytes) {
  if((address&15) || bytes<16) {
   aligned=address&~15ULL;n=16-(address&15);if(n>bytes)n=bytes;
   rc=c->io.rw(c->io.context,aligned,temp,16,0);if(rc)return fail_io(c,rc);
   if(write){memcpy(temp+(address&15),data,n);rc=c->io.rw(c->io.context,aligned,temp,16,1);if(rc)return fail_io(c,rc);}
   else memcpy(data,temp+(address&15),n);
  } else {
   n=bytes&~15ULL;if(n>4096)n=4096;
   rc=c->io.rw(c->io.context,address,data,n,write);if(rc)return fail_io(c,rc);
  }
  address+=n;data+=n;bytes-=n;
 }
 return 0;
}
static int hash_span(struct vxl_s8_ctx *c,vxl_u64 gpu,vxl_u64 bytes,vxl_u8 out[32]) {
 struct vxl_hunet_sha hash;vxl_u64 done=0,n;int rc;vxl_hunet_sha_init(&hash);
 while(done<bytes){n=bytes-done;if(n>4096)n=4096;rc=vxl_s8_io_bytes(c,gpu+done,c->engine->scratch,n,0);if(rc)return rc;vxl_hunet_sha_update(&hash,c->engine->scratch,n);done+=n;}
 vxl_hunet_sha_final(&hash,out);return 0;
}
int vxl_s8_program_register(struct vxl_s8_ctx *c,const struct vxl_s8_program *p) {
 vxl_u8 sha[32],nonzero;vxl_u32 end=0;const struct vxl_s8_binding_v1 *b;
 if(!c || !c->engine || !p || c->nonce_locked)return -EINVAL;
 b=&p->binding;
 if(!p->flat || !p->flat_bytes || p->flat_bytes>0xf0000 || p->flat_bytes>p->layout.runtime_bytes || p->entry!=0x80000000u || vxl_s8_layout_check(&p->layout))return -EINVAL;
 if(!p->mutable_ranges || !p->mutable_count || p->mutable_count>64 || p->input_count>544 || (!p->inputs && p->input_count))return -EINVAL;
 for(vxl_u32 i=0;i<p->mutable_count;++i) {
  const struct vxl_s8_mutable *m=&p->mutable_ranges[i];
  if(m->offset<end || m->offset>=p->layout.runtime_bytes || !m->length || m->length>p->layout.runtime_bytes-m->offset)return -EINVAL;
  end=m->offset+m->length;
 }
 if(b->magic!=VXL_S8_BINDING_MAGIC || b->version!=1 || b->bytes!=512 || b->profile!=5 || b->program!=11 || b->abi_major!=1 || b->features!=1 || b->reserved0)return -EINVAL;
 for(unsigned i=0;i<32;++i)if(b->reserved[i])return -EINVAL;
 for(unsigned i=0;i<14;++i){nonzero=0;for(unsigned j=0;j<32;++j)nonzero|=b->digests[i][j];if(!nonzero)return -ENODEV;}
 vxl_hunet_hash(p->flat,p->flat_bytes,sha);if(memcmp(sha,p->flat_sha,32) || memcmp(sha,b->digests[1],32))return -ESTALE;
 vxl_hunet_hash(b,512,sha);if(memcmp(sha,p->binding_sha,32) || memcmp(sha,c->binding_tag,16))return -ESTALE;
 c->program=p;return 0;
}
static int immutable_impl(struct vxl_s8_ctx *c) {
 const struct vxl_s8_program *p;vxl_u64 cursor=0,end,n;int rc;
 if(!c || !c->program)return -ENODEV;
 p=c->program;
 for(vxl_u32 span=0;span<=p->mutable_count;++span) {
  end=span==p->mutable_count?p->layout.runtime_bytes:p->mutable_ranges[span].offset;
  while(cursor<end) {
   n=end-cursor;if(n>4096)n=4096;
   rc=vxl_s8_io_bytes(c,0x80000000ULL+cursor,c->engine->scratch,n,0);if(rc)return rc;
   c->code_check_bytes+=n;
   for(vxl_u32 j=0;j<n;++j)if(c->engine->scratch[j]!=(cursor+j<p->flat_bytes?p->flat[cursor+j]:0))return -EACCES;
   cursor+=n;
  }
  if(span<p->mutable_count)cursor=p->mutable_ranges[span].offset+p->mutable_ranges[span].length;
 }
 return 0;
}
int vxl_s8_immutable_check(struct vxl_s8_ctx *c) {
 vxl_u64 begin,before;int rc;
 if(!c)return -ENODEV;
 begin=vxl_s8_now_ns(c);before=c->code_check_bytes;rc=immutable_impl(c);
 if(c->io.now_ns)vxl_s8_timing_add(c,VXL_S8_CODE_VALIDATE,begin,vxl_s8_now_ns(c),c->code_check_bytes-before);
 return rc;
}
static int restore_impl(struct vxl_engine *e,const struct vxl_s8_layout *layout) {
 struct vxl_s8_ctx *c;const struct vxl_s8_program *p;vxl_u64 cursor,end,n;int rc;
 if(!e || !e->s8 || !e->s8->program)return -ENODEV;
 c=e->s8;p=c->program;
 if(e->quarantined)return -EIO;
 if(!layout || vxl_s8_layout_check(layout) || memcmp(layout,&p->layout,sizeof(*layout)))return -ESTALE;
 c->restore_bytes=0;c->restore_ops=0;
 for(vxl_u32 span=0;span<p->mutable_count;++span) {
  cursor=p->mutable_ranges[span].offset;end=cursor+p->mutable_ranges[span].length;
  while(cursor<end) {
   n=end-cursor;if(n>4096)n=4096;
   for(vxl_u32 j=0;j<n;++j)e->scratch[j]=cursor+j<p->flat_bytes?p->flat[cursor+j]:0;
   rc=vxl_s8_io_bytes(c,0x80000000ULL+cursor,e->scratch,n,1);if(rc)return rc;
   c->restore_bytes+=n;c->restore_ops++;cursor+=n;
  }
 }
 return 0;
}
int vxl_s8_restore(struct vxl_engine *e,const struct vxl_s8_layout *layout) {
 struct vxl_s8_ctx *c;vxl_u64 begin;int rc;
 if(!e || !e->s8 || !e->s8->program)return -ENODEV;
 c=e->s8;if(e->quarantined)return -EIO;
 if(!layout || vxl_s8_layout_check(layout) || memcmp(layout,&c->program->layout,sizeof(*layout)))return -ESTALE;
 rc=vxl_s8_immutable_check(c);if(rc)return rc;
 begin=vxl_s8_now_ns(c);rc=restore_impl(e,layout);
 if(c->io.now_ns)vxl_s8_timing_add(c,VXL_S8_RESTORE,begin,vxl_s8_now_ns(c),c->restore_bytes);
 return rc;
}
vxl_u32 vxl_s8_input_bytes(const struct vxl_s8_job_v1 *j,unsigned which) {
 if(j->case_id==1)return 0;
 if(j->case_id==2)return 256;
 if(j->case_id==3)return which?64:128;
 if(j->case_id==4)return which?40:16;
 if(j->case_id==5)return which?32:16;
 return 16*j->k;
}
static int inputs_impl(struct vxl_s8_ctx *c,const struct vxl_s8_job_v1 *j) {
 vxl_u8 sha[32];const struct vxl_s8_program *p=c->program;int rc;
 for(vxl_u32 core=0;core<4;++core)if(j->core_mask&(1u<<core)) {
  vxl_u32 slot=core*4+j->owner_warp;const struct vxl_s8_input_digest *record=0;
  if(j->case_id==1)continue;
  for(vxl_u32 i=0;i<p->input_count;++i)if(p->inputs[i].case_id==j->case_id && p->inputs[i].slot==slot){if(record)return -EINVAL;record=&p->inputs[i];}
  if(!record)return -ENODEV;
  for(unsigned which=0;which<2;++which) {
   vxl_u32 bytes=vxl_s8_input_bytes(j,which);vxl_u64 gpu=(which?0x81200040ULL:0x81000040ULL)+slot*0x20000ULL;
   rc=hash_span(c,gpu,bytes,sha);if(rc)return rc;c->input_hash_bytes+=bytes;
   if(memcmp(sha,which?record->b_sha:record->a_sha,32))return -EBADMSG;
  }
 }
 return 0;
}
int vxl_s8_inputs_check(struct vxl_s8_ctx *c,const struct vxl_s8_job_v1 *j) {
 vxl_u64 begin,before;int rc;
 if(!c || !c->program || !j)return -ENODEV;
 begin=vxl_s8_now_ns(c);before=c->input_hash_bytes;rc=inputs_impl(c,j);
 if(c->io.now_ns)vxl_s8_timing_add(c,VXL_S8_INPUT_HASH,begin,vxl_s8_now_ns(c),c->input_hash_bytes-before);
 return rc;
}
static int guard_value(const struct vxl_s8_job_v1 *j,vxl_u32 region,vxl_u32 slot,vxl_u32 offset,int init) {
 vxl_u32 size=(region==3 || region==4)?0x20000:4096,index=0,local=0;int guarded=0;
 if(region==1) {
  if(offset<0xf0000)return -1;
  if(offset<0xf0040){guarded=1;local=offset-0xf0000;}
  else if(offset<0xf0080){guarded=1;local=offset-0xf0040;index=1;}
  else if(offset>=0xfffc0){guarded=1;local=offset-0xfffc0;index=2;}
 } else if(region==8) {
  if(offset>=0x80000){guarded=1;local=offset-0x80000;index=1;slot=0;}
  else {slot=63-offset/8192;local=offset%8192;if(local<64)guarded=1;else return init?0x6d:-1;}
 } else if(region==9) {
  if(offset>=0x400 && offset<0x440){guarded=1;local=offset-0x400;}
  else if(offset>=0x440 && offset<0x480){guarded=1;local=offset-0x440;index=1;}
  else if(offset>=0xfc0){guarded=1;local=offset-0xfc0;index=2;}
  else if(offset<0x400) {
   if(offset%256>=8 && offset%256<12)return init?0xff:-1;
   return 0;
  }
 } else {
  if(offset<64){guarded=1;local=offset;}
  else if(region==3 || region==4) {
   vxl_u32 bytes=vxl_s8_input_bytes(j,region==4);
   if(offset<64+bytes)return -1;
   if(offset<128+bytes){guarded=1;local=offset-64-bytes;index=1;}
   else if(offset>=size-64){guarded=1;local=offset-size+64;index=2;}
  } else if(region==2) {
   if(offset<192)return -1;
   if(offset<256){guarded=1;local=offset-192;index=1;}
   else if(offset>=4032){guarded=1;local=offset-4032;index=2;}
  } else if(offset>=4032){guarded=1;local=offset-4032;index=1;}
  else if(region==5) {
   int active=(slot%4==j->owner_warp && (j->core_mask&(1u<<(slot/4))));
   if(!init && active) {
    if((offset>=64 && offset<448) || (offset>=448 && offset<2240) || (offset>=2304 && offset<3200))return -1;
   }
   if((offset>=64 && offset<448) || (offset>=1216 && offset<1728) || (offset>=2304 && offset<3200))return 0xcc;
   if(offset>=448 && offset<2240)return 0;
  } else if(region==6 && offset<2304)return init?0:-1;
  else if(region==7 && offset<320)return init?0:-1;
 }
 return guarded?(int)vxl_s8_guard_byte(region,slot,index,local):0x6d;
}
static int guards_impl(struct vxl_s8_ctx *c,const struct vxl_s8_job_v1 *j,int init) {
 const struct vxl_s8_layout *l=&c->program->layout;int rc,changed;
 for(unsigned region=1;region<=9;++region) {
  unsigned slots=(region>=3 && region<=5)?16:1;
  vxl_u64 size=region==9?4096:l->regions[region-1].size/slots;
  for(unsigned slot=0;slot<slots;++slot)for(vxl_u64 offset=0;offset<size;offset+=4096) {
   vxl_u64 n=size-offset;if(n>4096)n=4096;
   vxl_u64 address=(region==9?0x10002000ULL:l->regions[region-1].begin)+slot*size+offset;
   rc=vxl_s8_io_bytes(c,address,c->engine->scratch,n,0);if(rc)return rc;changed=0;
   for(vxl_u32 i=0;i<n;++i) {
    int expected=guard_value(j,region,slot,(vxl_u32)(offset+i),init);
    if(expected<0)continue;
    if(init){c->engine->scratch[i]=(vxl_u8)expected;changed=1;}
    else if(c->engine->scratch[i]!=(vxl_u8)expected)return -EIO;
   }
   if(init && changed){rc=vxl_s8_io_bytes(c,address,c->engine->scratch,n,1);if(rc)return rc;c->guard_init_bytes+=n;}
   if(!init)c->guard_check_bytes+=n;
  }
 }
 return 0;
}
int vxl_s8_guards(struct vxl_s8_ctx *c,const struct vxl_s8_job_v1 *j,int init) {
 vxl_u64 begin,before,after;int rc;
 if(!c || !c->program || !j)return -ENODEV;
 begin=vxl_s8_now_ns(c);before=init?c->guard_init_bytes:c->guard_check_bytes;c->times.guard_depth++;
 rc=guards_impl(c,j,init);c->times.guard_depth--;after=init?c->guard_init_bytes:c->guard_check_bytes;
 if(c->io.now_ns)vxl_s8_timing_add(c,init?VXL_S8_GUARD_INIT:VXL_S8_GUARD_CHECK,begin,vxl_s8_now_ns(c),after-before);
 return rc;
}
int vxl_s8_prepare(struct vxl_engine *e,struct vxl_prepared *out) {
 struct vxl_s8_ctx *c;struct vxl_s8_job_v1 input,canonical;int rc;
 if(!e || !e->s8 || !e->s8->program || !out)return -ENODEV;
 c=e->s8;if(e->quarantined || e->job.pinned || e->job.state!=VXL_READY || c->capture_pending || c->delivery_fault)return -EBUSY;
 c->restore_bytes=c->restore_ops=c->code_check_bytes=c->input_hash_bytes=c->guard_init_bytes=0;
 rc=vxl_s8_io_bytes(c,0x80100040ULL,&input,128,0);if(rc)return rc;
 rc=vxl_s8_canonical_job(c,&input,&canonical);if(rc)return rc;
 rc=vxl_s8_trace_begin(c,&canonical,e->next_job+1);if(rc)return rc;
 rc=vxl_s8_inputs_check(c,&canonical);if(rc)return rc;
 rc=vxl_s8_restore(e,&c->program->layout);if(rc)return rc;
 rc=vxl_s8_guards(c,&canonical,1);if(rc)return rc;
 rc=vxl_s8_io_bytes(c,0x80100040ULL,&canonical,128,1);if(rc)return rc;
 rc=vxl_s8_accept_job(c,&canonical);if(rc)return rc;
 memset(out,0,sizeof(*out));out->program_id=11;out->code_gpu=0x80000000;out->args_gpu=0x80100040;
 out->args_word_count=32;memcpy(out->args_words,&canonical,128);return 0;
}

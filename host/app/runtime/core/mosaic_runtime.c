#include <vxl/mosaic_runtime.h>
#include <vxl/hunet.h>
static int nz(const vxl_u8 *p,unsigned n){vxl_u8 v=0;for(unsigned i=0;i<n;++i)v|=p[i];return v!=0;}
static struct vxl_object *obj(struct vxl_engine *e,vxl_u64 handle){for(unsigned i=0;i<VXL_MAX_OBJECTS;++i)if(handle&&e->objects[i].handle==handle)return &e->objects[i];return NULL;}
static int fail(struct mosaic_ctx *c,int rc){return mosaic_state_fault(c,rc);}
int mosaic_runtime_attach(struct mosaic_ctx *c,const struct mosaic_io *io){
 if(!c||!c->host||!c->host->program||!io||!io->rw||!io->read||!io->write||!io->ready||!io->cdma||!io->now_ns)return -EINVAL;
 struct vxl_engine *e=c->host->engine;
 if(!e||e->profile_id!=6)return -EXDEV;
 if(e->hunet||e->s8||e->mosaic||e->owner||e->opened||e->quarantined||e->job.pinned)return -EBUSY;
 c->host->io=*io;e->mosaic=c;return 0;
}
int mosaic_io_bytes(struct mosaic_ctx *c,vxl_u64 address,void *buffer,vxl_u64 bytes,int write){
 if(!c||!c->host||!c->host->io.rw||!buffer||!bytes)return -EINVAL;
 if(address<0x80000000ULL){if(address<VXL_PS_BASE||bytes>VXL_PS_SIZE||address-VXL_PS_BASE>VXL_PS_SIZE-bytes)return -ERANGE;}
 else if(address>=0x100000000ULL||bytes>0x100000000ULL-address)return -ERANGE;
 struct mosaic_host_state *h=c->host;vxl_u8 *p=buffer;int rc=0;vxl_u64 begin=h->io.now_ns(h->io.context);
 while(bytes){
  vxl_u64 count,aligned=address&~15ULL;
  if((address&15)||bytes<16){vxl_u8 block[16];count=16-(address&15);if(count>bytes)count=bytes;
   rc=h->io.rw(h->io.context,aligned,block,16,0);if(rc)break;
   if(write){memcpy(block+(address&15),p,count);rc=h->io.rw(h->io.context,aligned,block,16,1);}else memcpy(p,block+(address&15),count);
  }else{count=bytes&~15ULL;if(count>4096)count=4096;rc=h->io.rw(h->io.context,address,p,count,write);}
  if(rc)break;
  h->io_bytes+=count;address+=count;p+=count;bytes-=count;
 }
 vxl_u64 end=h->io.now_ns(h->io.context);if(end<begin)return -ENODATA;h->io_ns+=end-begin;return rc;
}
int mosaic_runtime_idle(struct mosaic_ctx *c){
 if(!c||!c->host||!c->host->io.read)return -EINVAL;
 struct mosaic_io *io=&c->host->io;vxl_u32 status=io->read(io->context,0xc);
 if(io->read(io->context,0)!=0x56580204||io->read(io->context,4)!=0x40404||io->read(io->context,0x30)!=15||io->read(io->context,0x38)!=0x10001)return -ENODEV;
 if(io->ready(io->context)!=1||!(io->read(io->context,0x34)&1)||((status>>8)&7)||(status&6)||io->read(io->context,0x1c)||io->read(io->context,0x20)||io->read(io->context,0x24)||(io->read(io->context,0x58)&1)||vxl_cdma_status(io->cdma(io->context))!=1)return -EBUSY;
 return 0;
}
static int hash_range(struct mosaic_ctx *c,vxl_u64 address,vxl_u32 bytes,const vxl_u8 expected[32],int finite){
 struct vxl_hunet_sha sha;vxl_u8 digest[32];struct vxl_engine *e=c->host->engine;vxl_hunet_sha_init(&sha);
 for(vxl_u32 off=0;off<bytes;){vxl_u32 n=bytes-off;if(n>4096)n=4096;int rc=mosaic_io_bytes(c,address+off,e->scratch,n,0);if(rc)return rc;if(finite){if(n%4)return -EINVAL;for(unsigned i=0;i<n;i+=4){vxl_u32 bits;memcpy(&bits,e->scratch+i,4);if((bits&0x7f800000u)==0x7f800000u)return -EDOM;}}vxl_hunet_sha_update(&sha,e->scratch,n);off+=n;}
 vxl_hunet_sha_final(&sha,digest);return memcmp(digest,expected,32)?-EACCES:0;
}
static int zero_range(struct mosaic_ctx *c,vxl_u64 address,vxl_u32 bytes){
 struct vxl_engine *e=c->host->engine;
 while(bytes){vxl_u32 n=bytes>4096?4096:bytes;memset(e->scratch,0,n);int rc=mosaic_io_bytes(c,address,e->scratch,n,1);if(rc)return rc;address+=n;bytes-=n;}return 0;
}
static int immutable(struct mosaic_ctx *c){
 struct mosaic_host_state *h=c->host;const struct mosaic_program *p=h->program;vxl_u64 cursor=0;vxl_u64 begin=h->io.now_ns(h->io.context);int rc=0;
 for(unsigned span=0;span<=p->mutable_count&&!rc;++span){vxl_u64 end=span==p->mutable_count?p->runtime_bytes:p->mutable_ranges[span].offset;
  while(cursor<end){vxl_u32 n=end-cursor;if(n>4096)n=4096;rc=mosaic_io_bytes(c,0x80000000ULL+cursor,h->engine->scratch,n,0);if(rc)break;
   for(vxl_u32 i=0;i<n;++i)if(h->engine->scratch[i]!=(cursor+i<p->flat_bytes?p->flat[cursor+i]:0)){rc=-EACCES;break;}
   cursor+=n;
  }if(span<p->mutable_count)cursor=p->mutable_ranges[span].offset+p->mutable_ranges[span].bytes;
 }
 if(!rc)rc=hash_range(c,0x80800000,p->assets_bytes,p->assets->package_sha,0);
 if(!rc&&p->rodata_bytes)rc=hash_range(c,p->rodata_gpu,p->rodata_bytes,p->rodata_sha,0);
 vxl_u64 end=h->io.now_ns(h->io.context);if(end<begin)return -ENODATA;h->immutable_ns+=end-begin;return rc;
}
static int restore_mutable(struct mosaic_ctx *c){
 struct mosaic_host_state *h=c->host;const struct mosaic_program *p=h->program;
 for(unsigned i=0;i<p->mutable_count;++i){vxl_u32 off=p->mutable_ranges[i].offset,left=p->mutable_ranges[i].bytes;
  while(left){vxl_u32 n=left>4096?4096:left;memset(h->engine->scratch,0,n);if(off<p->flat_bytes){vxl_u32 initialized=p->flat_bytes-off;if(initialized>n)initialized=n;memcpy(h->engine->scratch,p->flat+off,initialized);}int rc=mosaic_io_bytes(c,0x80000000ULL+off,h->engine->scratch,n,1);if(rc)return rc;off+=n;left-=n;}
 }return 0;
}
static int guard(struct mosaic_ctx *c,vxl_u64 address,vxl_u32 bytes,unsigned slot,int write){
 vxl_u8 expected[64],actual[64];struct mosaic_host_state *h=c->host;
 while(bytes){vxl_u32 n=bytes>64?64:bytes;for(unsigned j=0;j<n;++j)expected[j]=(vxl_u8)(0xa5u^(h->generation*3u+h->epoch*7u+slot*29u+j*17u));
  int rc=mosaic_io_bytes(c,address,write?expected:actual,n,write);if(rc)return rc;if(!write&&memcmp(expected,actual,n))return -EACCES;
  address+=n;bytes-=n;++slot;
 }return 0;
}
static int guards(struct mosaic_ctx *c,int write){
 struct mosaic_host_state *h=c->host;unsigned slot=0;int rc=0;vxl_u64 begin=h->io.now_ns(h->io.context);
 for(unsigned i=2;i<MOSAIC_ROLES&&!rc;++i){const struct mosaic_region *r=&c->layout->region[i];rc=guard(c,r->address-64,64,slot++,write);if(!rc)rc=guard(c,r->address+r->bytes,64,slot++,write);}
 for(unsigned hart=0;hart<64&&!rc;++hart)rc=guard(c,0xffff0000ULL-(hart+1)*8192ULL,64,slot++,write);
 if(!rc)rc=guard(c,0xffff0000,64,slot++,write);
 if(!rc)rc=guard(c,0x10002400,128,slot,write);
 slot+=2;
 if(!rc)rc=guard(c,0x10002fc0,64,slot++,write);
 if(!rc)rc=guard(c,0x800fffc0,64,slot++,write);
 if(!rc)rc=guard(c,0x80100000,64,slot++,write);
 if(!rc)rc=guard(c,0x801000c0,64,slot,write);
 vxl_u64 end=h->io.now_ns(h->io.context);if(end<begin)return -ENODATA;h->guard_ns+=end-begin;return rc;
}
int mosaic_runtime_seal(struct mosaic_ctx *c,const struct vxl_mosaic_seal_v1 *s){
 if(!c||!c->host||!s)return -EINVAL;
 struct mosaic_host_state *h=c->host;struct vxl_engine *e=h->engine;const struct mosaic_program *p=h->program;
 if(!p||e->mosaic!=c||!e->opened||!e->owner)return -ENODEV;
 if(h->loaded||h->frame_active||e->quarantined||e->job.pinned||e->job.state!=VXL_READY)return -EBUSY;
 if(s->magic!=VXL_MOSAIC_SEAL_MAGIC||s->version!=1||s->bytes!=128||s->flags||s->layout>=p->layout_count||s->generation!=e->generation||!nz(s->nonce,16)||nz(s->reserved,16))return -EINVAL;
 if(memcmp(s->binding_sha,p->binding_sha,32))return -ESTALE;
 const struct mosaic_layout *l=&p->layouts[s->layout].layout;const vxl_u32 gpu[5]={0x80000000,0x80800000,0x82000000,0xfff00000,0};const vxl_u64 size[5]={0x800000,0x1000000,l->end-0x82000000,0x100000,128};
 for(unsigned i=0;i<5;++i){struct vxl_object *o=obj(e,s->handles[i]);if(!o||o->length!=size[i]||o->gpu!=gpu[i]||o->domain!=(i==4?VXL_PS:VXL_PL)||o->purpose!=(i==0?VXL_CODE:i==4?VXL_ARGS:VXL_DATA)||o->allocated!=(i==4?0:size[i]))return -EINVAL;for(unsigned j=0;j<i;++j)if(s->handles[j]==s->handles[i])return -EINVAL;}
 unsigned count=0;for(unsigned i=0;i<VXL_MAX_OBJECTS;++i)count+=e->objects[i].handle!=0;if(count!=5)return -EINVAL;
 h->owner=e->owner;h->generation=e->generation;
 int rc=mosaic_runtime_idle(c);if(rc)return fail(c,rc);
 rc=hash_range(c,0x80000000,p->flat_bytes,p->binding.digests[MOSAIC_FLAT_SHA],0);
 if(!rc)rc=hash_range(c,0x80800000,p->assets_bytes,p->assets->package_sha,0);
 if(!rc&&p->rodata_bytes)rc=hash_range(c,p->rodata_gpu,p->rodata_bytes,p->rodata_sha,0);
 if(!rc)rc=zero_range(c,0x80000000ULL+p->flat_bytes,p->runtime_bytes-p->flat_bytes);
 if(rc)return fail(c,rc);
 memcpy(h->handles,s->handles,sizeof(h->handles));memcpy(h->nonce,s->nonce,16);h->sealed_layout=s->layout;h->nonce_locked=h->loaded=1;c->layout=l;return 0;
}
static int frame_check(struct mosaic_ctx *c,const struct vxl_mosaic_frame_v1 *p){
 struct mosaic_host_state *h=c->host;
 if(!p||p->magic!=VXL_MOSAIC_FRAME_MAGIC||p->version!=1||p->bytes!=128||(p->flags&~1u)||p->reserved0||nz(p->reserved,32)||!nz(p->run_id,16)||!nz(p->input_sha,32))return -EINVAL;
 if(p->layout!=h->sealed_layout||p->generation!=h->engine->generation||memcmp(p->binding_tag,h->program->binding_sha,16))return -ESTALE;
 return 0;
}
int mosaic_runtime_begin(struct mosaic_ctx *c,const struct vxl_mosaic_frame_v1 *p){
 if(!c||!c->host||!c->host->loaded)return -ENODEV;
 struct mosaic_host_state *h=c->host;int rc=frame_check(c,p);if(rc)return rc;
 if(h->frame_active||h->evidence_pending||h->job_active)return -EBUSY;
 if(h->input_written!=c->layout->width*c->layout->height*12u)return -EINVAL;
 rc=mosaic_runtime_idle(c);if(rc)return fail(c,rc);
 rc=hash_range(c,c->layout->region[MOSAIC_FP_A].address,h->input_written,p->input_sha,1);if(rc)return rc==-EIO||rc==-ETIMEDOUT||rc==-ENODATA?fail(c,rc):rc;
 rc=mosaic_frame_begin(c,h->engine->owner,p->run_id,p->layout,p->input_epoch,p->flags);if(rc)return rc;
 memcpy(h->input_sha,p->input_sha,32);rc=guards(c,1);if(rc)return fail(c,rc);return 0;
}
int mosaic_runtime_end(struct mosaic_ctx *c,const struct vxl_mosaic_frame_v1 *p){
 if(!c||!c->host||!c->host->loaded)return -ENODEV;
 struct mosaic_host_state *h=c->host;int rc=frame_check(c,p);if(rc)return rc;
 if(p->input_epoch!=h->epoch||memcmp(p->run_id,h->run_id,16)||memcmp(p->input_sha,h->input_sha,32)||p->flags!=h->diagnostics)return -ESTALE;
 if(h->phase!=77||h->job_active||h->evidence_pending)return -EBUSY;
 rc=mosaic_runtime_idle(c);if(!rc)rc=guards(c,0);if(!rc)rc=immutable(c);if(rc)return fail(c,rc);
 rc=mosaic_frame_end(c,h->engine->owner);if(!rc){h->input_written=0;memset(h->args_shadow,0,128);}return rc;
}
int mosaic_prepare(struct vxl_engine *e,struct vxl_prepared *prepared){
 if(!e||!e->mosaic||!prepared)return -EINVAL;
 struct mosaic_ctx *c=e->mosaic;struct mosaic_host_state *h=c->host;struct mosaic_job_v1 job,expected;int rc;
 if(!h->loaded||!h->frame_active||h->job_active||h->evidence_pending||e->quarantined||e->job.state!=VXL_READY)return -EBUSY;
 memcpy(&job,h->args_shadow,128);rc=mosaic_next_job(c,job.tile_count,&expected);if(rc)return rc;
 rc=mosaic_validate_job(c,&job);if(rc)return rc;if(memcmp(&job,&expected,128))return -ESTALE;
 if(e->next_job==~(vxl_u64)0||!h->submit_enter_ns)return -EOVERFLOW;
 rc=mosaic_runtime_idle(c);if(rc)return fail(c,rc);
 vxl_u32 before=h->io.read(h->io.context,0x2c);rc=mosaic_submit_begin(c,e->owner,e->next_job+1,before,&job,h->submit_enter_ns);if(rc)return rc;++e->next_job;
 h->io_ns=h->io_bytes=h->guard_ns=h->immutable_ns=h->poll_ns=h->handoff_ns=0;
 h->evidence.observed.prepare_enter=h->io.now_ns(h->io.context);
 rc=guards(c,0);if(!rc)rc=immutable(c);if(!rc)rc=restore_mutable(c);
 if(!rc)rc=zero_range(c,0x10002000,1024);
 for(unsigned core=0;core<4&&!rc;++core){vxl_u32 sentinel=0xfedcba98u;rc=mosaic_io_bytes(c,0x10002008+core*256,&sentinel,4,1);vxl_u8 empty[64];memset(empty,0xfe,64);if(!rc)rc=mosaic_io_bytes(c,0x10002800+core*64,empty,64,1);}
 if(!rc)rc=mosaic_io_bytes(c,MOSAIC_ARGS_GPU,&job,128,1);
 h->evidence.observed.prepare_done=h->io.now_ns(h->io.context);
 if(rc)return fail(c,rc);
 *prepared=(struct vxl_prepared){.program_id=12,.code_gpu=MOSAIC_CODE_GPU,.args_gpu=MOSAIC_ARGS_GPU,.args_word_count=32};memcpy(prepared->args_words,&job,128);return 0;
}
int mosaic_runtime_start(struct mosaic_ctx *c){
 if(!c||!c->host)return -EINVAL;
 struct mosaic_host_state *h=c->host;struct mosaic_io *io=&h->io;
 if(!h->job_active||h->evidence.observed.start||h->first_error)return h->first_error?h->first_error:-EBUSY;
 int rc=mosaic_runtime_idle(c);if(rc)return fail(c,rc);
 io->write(io->context,0x18,4000000000u);if(io->read(io->context,0x18)!=4000000000u)return fail(c,-EIO);
 io->write(io->context,0x10,MOSAIC_CODE_GPU);io->write(io->context,0x14,MOSAIC_ARGS_GPU);io->write(io->context,0x50,0);
 vxl_u64 now=io->now_ns(io->context);if(now>~(vxl_u64)0-60000000000ULL)return fail(c,-EOVERFLOW);
 h->deadline_ns=now+60000000000ULL;h->evidence.observed.start=now;io->write(io->context,8,1);return 0;
}
static void metadata(const struct mosaic_job_v1 *j,unsigned core,vxl_u8 out[64]){
 memcpy(out,j->run_id,16);memcpy(out+16,j->binding_tag,16);vxl_u32 fields[8]={j->generation,j->sequence,j->op,j->layer,j->tile_first,j->tile_count,j->input_epoch,core};
 for(unsigned i=0;i<8;++i)for(unsigned b=0;b<4;++b)out[32+i*4+b]=(vxl_u8)(fields[i]>>(b*8));
}
int mosaic_complete(struct vxl_engine *e,const struct vxl_event *event){
 if(!e||!e->mosaic||!event)return -EINVAL;
 struct mosaic_host_state *h=e->mosaic->host;if(memcmp(event,&h->evidence.observed.before_ack,sizeof(*event)))return fail(e->mosaic,-ESTALE);
 return mosaic_state_complete(e->mosaic,&h->evidence.observed);
}
int mosaic_runtime_poll(struct vxl_engine *e){
 if(!e||!e->mosaic)return -EINVAL;
 struct mosaic_ctx *c=e->mosaic;struct mosaic_host_state *h=c->host;struct mosaic_io *io=&h->io;
 if(h->first_error)return h->first_error;
 if(!h->job_active||!h->evidence.observed.start)return 0;
 vxl_u64 begin=io->now_ns(io->context);struct mosaic_completion p=h->evidence.observed;struct vxl_event *v=&p.before_ack;
 *v=(struct vxl_event){.kind=VXL_DONE,.id=e->job.id,.status=io->read(io->context,0xc),.errors=io->read(io->context,0x1c),.pending_w=io->read(io->context,0x20),.pending_r=io->read(io->context,0x24),.jobs=io->read(io->context,0x2c),.ready=io->ready(io->context)==1&&(io->read(io->context,0x34)&1)};
 v->idle=((v->status>>8)&7)==0;p.generation=e->generation;p.sequence=h->sequence;p.elapsed_raw=io->read(io->context,0x28);p.elapsed_address=0xa0000028ULL;p.watchdog=io->read(io->context,0x18);p.cdma_status=io->cdma(io->context);p.tex_pending=io->read(io->context,0x58)&1;e->snapshot=*v;
 if(v->errors||(v->status&4)||vxl_cdma_status(p.cdma_status)<0){h->evidence.observed=p;return fail(c,-EIO);}
 if(!(v->status&2)){vxl_u64 end=io->now_ns(io->context);h->poll_ns+=end-begin;h->evidence.observed=p;if(end>=h->deadline_ns)return fail(c,-ETIMEDOUT);return 0;}
 p.done_seen=io->now_ns(io->context);h->poll_ns+=p.done_seen-begin;
 if(v->idle||((v->status>>8)&7)!=5||!v->ready||v->pending_w||v->pending_r||v->jobs!=e->job.jobs_before+1u||p.tex_pending||vxl_cdma_status(p.cdma_status)!=1||!p.elapsed_raw||p.elapsed_raw>=4000000000u||p.watchdog!=4000000000u){h->evidence.observed=p;return fail(c,-EIO);}
 int rc=guards(c,0);if(!rc)p.checks|=MOSAIC_CHECK_GUARDS;if(!rc)rc=immutable(c);if(!rc)p.checks|=MOSAIC_CHECK_IMMUTABLE;
 vxl_u8 raw_args[128];if(!rc)rc=mosaic_io_bytes(c,MOSAIC_ARGS_GPU,raw_args,128,0);if(!rc&&memcmp(raw_args,&h->evidence.args,128))rc=-ESTALE;if(!rc)p.checks|=MOSAIC_CHECK_INPUT;
 for(unsigned core=0;core<4&&!rc;++core){rc=mosaic_io_bytes(c,0x10002008+core*256,&p.exitcode[core],4,0);if(!rc&&p.exitcode[core])rc=-EIO;vxl_u8 actual[64],expected[64];metadata(&h->evidence.args,core,expected);if(!rc)rc=mosaic_io_bytes(c,0x10002800+core*64,actual,64,0);if(!rc&&memcmp(expected,actual,64))rc=-ESTALE;if(!rc)p.exit_valid|=1u<<core;}
 if(!rc)p.checks|=MOSAIC_CHECK_EXITS|MOSAIC_CHECK_IDENTITY;
 if(rc){h->evidence.observed=p;return fail(c,rc);}
 vxl_u64 handoff=io->now_ns(io->context);io->write(io->context,8,2);vxl_u32 after=io->read(io->context,0xc);p.ack_idle=((after>>8)&7)==0&&!(after&6)&&!io->read(io->context,0x1c)&&!io->read(io->context,0x20)&&!io->read(io->context,0x24);p.ack_ready=io->ready(io->context)==1&&(io->read(io->context,0x34)&1);p.ack_done=io->now_ns(io->context);h->handoff_ns+=p.ack_done-handoff;
 if(p.ack_idle&&p.ack_ready)p.checks|=MOSAIC_CHECK_ACK;
 p.checks|=MOSAIC_CHECK_TIMING;p.complete=io->now_ns(io->context);h->evidence.observed=p;
 rc=mosaic_complete(e,v);return rc?rc:1;
}

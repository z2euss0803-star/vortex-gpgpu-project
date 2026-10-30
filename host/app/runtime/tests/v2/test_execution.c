/* Native execution boundary test. Register/DMA IO is a fixture, not a board. */
#define main provider_component_main
#include "test_provider.c"
#undef main
#include <vxl/hunet.h>
#include <vxl/v2/execution.h>
#include <vxl/v2/startup.h>
static vxl_u64 ns(void *p){return ++((struct hardware *)p)->ms*1000+1;}
static int raw_execution_snapshot(void *p,vxl_u64 seq,struct vxl_v2_snapshot *s){return vxl_v2_execution_snapshot(p,seq,s);}
static void execution_write(void *p,vxl_u32 off,vxl_u32 value){struct hardware *h=p;write_reg(p,off,value);if(off==8){h->regs[3]=value==1?0x100:0;if(value==2 && h->ack_fault){h->regs[3]=4;h->regs[0x1c/4]=0x80;}}}
static void lifecycle(const char *path,unsigned fault,int graphics){
 struct hardware io={.locked=1,.completed=1};struct vxl_v2_hw h={0};struct vxl_v2_ctx c;
 struct vxl_v2_program_table *table=calloc(1,sizeof(*table));struct vxl_v2_queue *q=calloc(1,sizeof(*q));struct vxl_v2_execution *x=calloc(1,sizeof(*x));
 struct vxl_v2_catalog cat;struct vxl_v2_catalog_entry e;vxl_u8 raw[2048],code[64]={0};FILE *f=fopen(path,"rb");size_t n;vxl_u32 arg=1;vxl_u64 seq;struct vxl_v2_program_ref program;struct vxl_v2_ref args,data;struct vxl_v2_snapshot snap;struct vxl_v2_completion completion;
 struct vxl_v2_execution_policy policy={.watchdog=4000000000u,.retire_ref=VXL_V2_ABSENT,.retire_offset=8,.retire_stride=256,.retire_count=4,.directory_ref=VXL_V2_ABSENT,.indices_ref=VXL_V2_ABSENT,.guard_count=1,.guards={{0x100ff000,0,64,1024,4}}};
 struct vxl_v2_job j={.major=2,.size=512,.cookie=1,.device_generation=1,.execution_budget_ns=1000000000,.args_bytes=4,.num_refs=1};
 assert(f && table && q && x);n=fread(raw,1,sizeof(raw),f);fclose(f);io.ps=calloc(1,VXL_PS_SIZE);assert(io.ps);
 h.ctx=&c;h.identity=graphics?p8():s8();h.ps=io.ps;h.ps_dma=VXL_PS_BASE;h.os=&io;h.locked=locked;h.ready=ready;h.read=read_reg;h.write=execution_write;h.memory_barrier=barrier;h.now_ns=ns;h.cdma=(struct vxl_cdma_io){&io,dma_read,dma_write,dma_now,dma_pause,0};io.dma[1]=2;io.regs[0]=0x56580204;io.regs[1]=0x40404;io.regs[0x34/4]=1;
 vxl_v2_init(&c,&vxl_v2_native_memory_ops,&h);c.guard_bytes=64;
 assert(vxl_v2_catalog_open(&cat,raw,(vxl_u32)n,&h.identity.provider)==0);assert(vxl_v2_catalog_lookup(&cat,raw+320,&e)==0);memcpy(policy.entry_id,e.entry_id,32);
 if(h.identity.provider.flags==2){policy.directory_ref=1;policy.indices_ref=2;policy.faces_arg_offset=0;policy.tile_count=2;policy.max_faces=30926;policy.max_indices=262144;}
 {struct {struct vxl_v2_execution_header header;struct vxl_v2_execution_policy policy;} manifest;vxl_u8 sha[32];
  memset(&manifest,0,sizeof(manifest));manifest.header.magic=VXL_V2_EXEC_MAGIC;manifest.header.version=1;manifest.header.bytes=sizeof(manifest);manifest.header.count=1;memcpy(manifest.header.catalog_sha,cat.sha,32);manifest.policy=policy;vxl_hunet_hash(&manifest,sizeof(manifest),sha);
  io.regs[0x30/4]=15;io.regs[0x38/4]=0x10001;
  if(h.identity.provider.flags==2){assert(vxl_v2_boot_bind(x,&h,&cat,raw,(vxl_u32)n,&manifest,sizeof(manifest),cat.sha,sha)==-ENODEV);io.regs[0x60/4]=0x41584931;io.regs[0x64/4]=1;io.regs[0x68/4]=4;}
  assert(vxl_v2_boot_bind(x,&h,&cat,raw,(vxl_u32)n,&manifest,sizeof(manifest),cat.sha,sha)==0);
  io.regs[3]=2;assert(vxl_v2_boot_bind(x,&h,&cat,raw,(vxl_u32)n,&manifest,sizeof(manifest),cat.sha,sha)==-EBUSY);io.regs[3]=0;
  manifest.header.catalog_sha[0]^=1;assert(vxl_v2_boot_bind(x,&h,&cat,raw,(vxl_u32)n,&manifest,sizeof(manifest),cat.sha,sha)==-EBADMSG);
 }
 assert(vxl_v2_table_bind(&c,table,&cat)==0);assert(vxl_v2_queue_bind(&c,q,&vxl_v2_native_execution_ops,x)==0);q->snapshot=raw_execution_snapshot;q->metrics=&x->last;assert(vxl_v2_owner_enter(&c,1,1)==0);
 assert(vxl_v2_program_load(&c,1,&e,code,0,&program)==0);assert(vxl_v2_buffer_create(&c,1,1,1,2,3,4,&args)==0);assert(vxl_v2_buffer_copy(&c,1,&args,&arg,1)==0);
 assert(vxl_v2_buffer_create(&c,1,1,1,3,3,64,&data)==0);data.access=1;j.refs[0]=data;j.program_handle=program.handle;j.program_generation=program.generation;j.args_handle=args.handle;j.args_generation=args.object_generation;
 if(h.identity.provider.flags==2){vxl_u32 directory[4]={0,1,1,1},indices[4]={0};if(fault==10)directory[2]=0;
  assert(vxl_v2_buffer_create(&c,1,1,2,3,3,16,&j.refs[1])==0 && vxl_v2_buffer_copy(&c,1,&j.refs[1],directory,1)==0);
  assert(vxl_v2_buffer_create(&c,1,1,2,3,3,16,&j.refs[2])==0 && vxl_v2_buffer_copy(&c,1,&j.refs[2],indices,1)==0);
  j.refs[1].access=j.refs[2].access=1;j.num_refs=3;
 }
 assert(vxl_v2_accept(&c,1,&j,&seq)==0);
 if(fault==1)io.regs[3]=0x100;
 if(fault==2)io.ps[0]^=1;
 if(fault==11)io.completed=0;
 if(fault==12)io.regs[3]=2;
 if(fault==1 || fault==2 || (fault>=10 && fault<=12)){int expected=(fault==1 || fault==12)?-EBUSY:fault==2?-EBADMSG:fault==10?-ERANGE:-ETIMEDOUT;assert(vxl_v2_pump(&c)==expected);assert(c.quarantined && io.regs[2]!=1);assert(vxl_v2_read_completion(&c,1,seq,1,&completion)==0 && completion.result==expected && completion.status==(fault==1?0x100u:fault==12?2u:0u));goto done;}
 assert(vxl_v2_pump(&c)==0 && q->active_slot>=0 && x->started);
 assert(*(vxl_u32 *)(io.ps+0x10000)==arg && io.regs[0x10/4]==e.entry_pc && io.regs[0x14/4]==e.args_gpu);
 io.ms++;io.regs[3]=2;io.regs[0x2c/4]++;io.regs[0x28/4]=37;
 for(unsigned core=0;core<4;core++)memset(io.ps+0x2008+core*256,0,4);
 if(fault==3)io.ps[0x10000]^=1;
 if(fault==4){struct vxl_v2_object *o;assert(vxl_v2_ref_resolve(&c,1,&data,1,&o)==0);io.ps[o->offset]^=1;}
 if(fault==5){struct vxl_v2_object *o;assert(vxl_v2_ref_resolve(&c,1,&data,1,&o)==0);io.ps[o->offset+64]^=1;}
 if(fault==6)memset(io.ps+0x2008+256,0xff,4);
 if(fault==7)io.regs[0x20/4]=1;
 if(fault==8)io.regs[0x18/4]--;
 if(fault==9)io.ps[0xff000]^=1;
 if(fault==14)io.ack_fault=1;
 if(fault==15)io.ps[0xff000+1024]^=1;
 assert(vxl_v2_execution_snapshot(x,seq,&snap)==0);
 if(fault==7){assert(vxl_v2_poll(&c,&snap)==0 && c.active && io.regs[2]==1);io.regs[0x20/4]=0;assert(vxl_v2_execution_snapshot(x,seq,&snap)==0);}
 if(fault>=3 && fault!=7){assert(vxl_v2_poll(&c,&snap)<0 && c.quarantined && io.regs[2]==(fault==14?2u:1u));assert(vxl_v2_read_completion(&c,1,seq,1,&completion)==0 && completion.status==(fault==14?4u:2u) && !completion.cycles_valid);if(fault==14)assert(completion.errors==0x80);assert(vxl_v2_ack_reap(&c,1,seq,1)==-EIO);}
 else {assert(vxl_v2_poll(&c,&snap)==0 && !c.active && io.regs[2]==2);assert(vxl_v2_read_completion(&c,1,seq,1,&completion)==0 && completion.cycles_valid && completion.elapsed_raw==37);assert(completion.handoff_ns==x->last.handoff_ns && completion.guard_ns==x->last.guard_ns && completion.digest_ns==x->last.digest_ns && completion.io_bytes==x->last.io_bytes && completion.handoff_ns>0);assert(vxl_v2_ack_reap(&c,1,seq,1)==0);}
done:free(io.ps);free(table);free(q);free(x);
}
int main(int argc,char **argv){
 struct hardware io={.locked=1,.completed=1};struct vxl_v2_ctx c;
 struct vxl_v2_hw h={0};struct vxl_v2_object *o;struct vxl_v2_ref ref;
 io.ps=calloc(1,VXL_PS_SIZE);assert(io.ps);h.ctx=&c;h.identity=s8();h.ps=io.ps;h.ps_dma=VXL_PS_BASE;h.os=&io;h.locked=locked;h.ready=ready;h.read=read_reg;h.write=write_reg;h.memory_barrier=barrier;
 h.cdma=(struct vxl_cdma_io){&io,dma_read,dma_write,dma_now,dma_pause,0};io.dma[1]=2;io.regs[0]=0x56580204;io.regs[1]=0x40404;io.regs[0x34/4]=1;
 vxl_v2_init(&c,&vxl_v2_native_memory_ops,&h);assert(vxl_v2_owner_enter(&c,1,1)==0);
 /* Native public buffers must have checked head AND tail guards; a corrupt
  * guard must prevent a future START, not merely appear in a diagnostic log. */
 c.guard_bytes=64;assert(vxl_v2_buffer_create(&c,1,1,1,3,3,65,&ref)==0);
 assert(vxl_v2_ref_resolve(&c,1,&ref,3,&o)==0);
 assert(o->guard_bytes==64 && o->allocated==256 && o->bus==VXL_PS_BASE+o->offset+64);
 assert(vxl_v2_native_guards(&h,o,0)==0);
 /* Byte-exact boundary, including the alignment padding before tail guard. */
 assert(io.ps[o->offset+64+65]==0xa7);io.ps[o->offset+64+65]^=1;assert(vxl_v2_native_guards(&h,o,0)==-EBADMSG);io.ps[o->offset+64+65]^=1;
 io.ps[o->offset]^=1;assert(vxl_v2_native_guards(&h,o,0)==-EBADMSG);
 {vxl_u8 raw[2048];FILE *f;size_t n;struct vxl_v2_catalog cat;struct vxl_v2_execution *x=calloc(1,sizeof(*x));struct vxl_v2_execution_policy policy={.watchdog=4000000000u,.retire_ref=VXL_V2_ABSENT,.retire_offset=8,.retire_stride=256,.retire_count=4,.directory_ref=VXL_V2_ABSENT,.indices_ref=VXL_V2_ABSENT,.guard_count=1,.guards={{0x100ff000,0,64,1024,4}}};
  assert(argc==3 && x);f=fopen(argv[1],"rb");assert(f);n=fread(raw,1,sizeof(raw),f);fclose(f);
  assert(vxl_v2_catalog_open(&cat,raw,(vxl_u32)n,&h.identity.provider)==0);memcpy(policy.entry_id,raw+320,32);
  assert(vxl_v2_execution_bind(x,&h,&cat,&policy,1)==0);
  policy.watchdog=0;assert(vxl_v2_execution_bind(x,&h,&cat,&policy,1)==-EINVAL);free(x);
 }
 free(io.ps);for(unsigned fault=0;fault<10;fault++){lifecycle(argv[1],fault,0);lifecycle(argv[2],fault,1);}
 lifecycle(argv[2],10,1);lifecycle(argv[2],11,1);
 lifecycle(argv[1],12,0);lifecycle(argv[2],12,1);
 lifecycle(argv[1],14,0);lifecycle(argv[2],14,1);
 lifecycle(argv[1],15,0);lifecycle(argv[2],15,1);
 puts("V2_NATIVE_START_GUARDS_SOURCE_ARGS_RETIRE_DRAIN_ACK_AND_RAW_SNAPSHOT_PASS_NOT_BOARD");return 0;
}

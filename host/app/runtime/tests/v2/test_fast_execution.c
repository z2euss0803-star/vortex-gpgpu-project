/* Real native C execution path; fake MMIO/DMA is not a GPU/board proof. */
#define main provider_fixture_main
#include "test_provider.c"
#undef main
#include <vxl/v2/execution.h>
#ifndef VXL_V2_JOB_FAST_RUNTIME
#define VXL_V2_JOB_FAST_RUNTIME 1u
#endif
static unsigned source_reads,args_reads,acks;
static vxl_u64 clock_fast(void *p){return ++((struct hardware *)p)->ms*1000+1;}
static int tracked_copy(void *p,struct vxl_v2_object *o,vxl_u64 off,void *b,vxl_u64 n,int write){
 if(!write && (o->purpose==VXL_V2_CODE || o->purpose==VXL_V2_READONLY))source_reads++;
 if(!write && o->purpose==VXL_V2_ARGS && !off && n==o->length)args_reads++;
 return vxl_v2_native_memory_ops.copy(p,o,off,b,n,write);
}
static void write_fast(void *p,vxl_u32 off,vxl_u32 value){struct hardware *h=p;write_reg(p,off,value);if(off==8){h->regs[3]=value==1?0x100:0;if(value==2){acks++;if(h->ack_fault){h->regs[3]=4;h->regs[7]=0x80;}}}}
static int raw_fast(void *p,vxl_u64 seq,struct vxl_v2_snapshot *s){return vxl_v2_execution_snapshot(p,seq,s);}
static void check(const char *path,int fast,unsigned fault){
 struct hardware io={.locked=1,.completed=1};struct vxl_v2_hw h={0};struct vxl_v2_ctx c;
 struct vxl_v2_program_table *table=calloc(1,sizeof(*table));struct vxl_v2_queue *q=calloc(1,sizeof(*q));struct vxl_v2_execution *x=calloc(1,sizeof(*x));
 struct vxl_v2_catalog cat;struct vxl_v2_catalog_entry e;vxl_u8 raw[2048],code[64]={0};FILE *f=fopen(path,"rb");size_t n;vxl_u32 arg=1;vxl_u64 seq;struct vxl_v2_program_ref program;struct vxl_v2_ref args,data;struct vxl_v2_snapshot snap;struct vxl_v2_completion done;struct vxl_v2_object *o;
 struct vxl_v2_execution_policy policy={.watchdog=4000000000u,.retire_ref=VXL_V2_ABSENT,.retire_offset=8,.retire_stride=256,.retire_count=4,.directory_ref=VXL_V2_ABSENT,.indices_ref=VXL_V2_ABSENT,.guard_count=1,.guards={{0x100ff000,0,64,1024,4}}};
 struct vxl_v2_job j={.major=2,.size=512,.cookie=1,.device_generation=1,.execution_budget_ns=1000000000,.args_bytes=4,.num_refs=1};
 struct vxl_v2_memory_ops memory=vxl_v2_native_memory_ops;memory.copy=tracked_copy;
 assert(f&&table&&q&&x);n=fread(raw,1,sizeof(raw),f);fclose(f);io.ps=calloc(1,VXL_PS_SIZE);assert(io.ps);
 h.ctx=&c;h.identity=s8();h.ps=io.ps;h.ps_dma=VXL_PS_BASE;h.os=&io;h.locked=locked;h.ready=ready;h.read=read_reg;h.write=write_fast;h.memory_barrier=barrier;h.now_ns=clock_fast;h.cdma=(struct vxl_cdma_io){&io,dma_read,dma_write,dma_now,dma_pause,0};io.dma[1]=2;io.regs[0]=0x56580204;io.regs[1]=0x40404;io.regs[0x34/4]=1;
 vxl_v2_init(&c,&memory,&h);c.guard_bytes=64;assert(!vxl_v2_catalog_open(&cat,raw,(vxl_u32)n,&h.identity.provider));assert(!vxl_v2_catalog_lookup(&cat,raw+320,&e));memcpy(policy.entry_id,e.entry_id,32);
 assert(!vxl_v2_execution_bind(x,&h,&cat,&policy,1));assert(!vxl_v2_table_bind(&c,table,&cat));assert(!vxl_v2_queue_bind(&c,q,&vxl_v2_native_execution_ops,x));q->snapshot=raw_fast;q->metrics=&x->last;
#ifdef VXL_V2_FAST_RUNTIME_API
 q->fast_supported=fault!=9;
#endif
 assert(!vxl_v2_owner_enter(&c,1,1));assert(!vxl_v2_program_load(&c,1,&e,code,0,&program));assert(!vxl_v2_buffer_create(&c,1,1,1,2,3,4,&args));assert(!vxl_v2_buffer_copy(&c,1,&args,&arg,1));assert(!vxl_v2_buffer_create(&c,1,1,1,3,3,64,&data));data.access=1;
 j.flags=fast?VXL_V2_JOB_FAST_RUNTIME:0;j.refs[0]=data;j.program_handle=program.handle;j.program_generation=program.generation;j.args_handle=args.handle;j.args_generation=args.object_generation;
 source_reads=args_reads=acks=0;
 if(fault==9){assert(vxl_v2_accept(&c,1,&j,&seq)==-EOPNOTSUPP);assert(!acks&&!x->started);goto end;}
 assert(vxl_v2_accept(&c,1,&j,&seq)==0);source_reads=args_reads=0;assert(!vxl_v2_pump(&c));
 io.regs[3]=0x502;io.regs[0x2c/4]=x->jobs_before+1;io.regs[0x28/4]=37;io.dma[1]=0x1002;memset(io.ps+0x2008,0,4);for(unsigned k=1;k<4;k++)memset(io.ps+0x2008+k*256,0,4);
 assert(!vxl_v2_ref_resolve(&c,1,&data,1,&o));
 if(fault==1)io.ps[o->offset+64]^=1; /* intentionally unchecked in FAST */
 if(fault==2)io.ps[e.args_gpu-VXL_PS_BASE]^=1; /* device args body, not schema */
 if(fault==3)o->generation++;
 if(fault==4)io.ps[o->offset]^=1;
 if(fault==5)memset(io.ps+0x2008+256,0xff,4);
 if(fault==6)io.regs[7]=0x80;
 if(fault==7)io.regs[0x20/4]=1;
 if(fault==8)io.ack_fault=1;
 int snapshot_rc=vxl_v2_execution_snapshot(x,seq,&snap);assert(snapshot_rc==(fault==3?-ESTALE:0));
 if(fault==7){assert(!vxl_v2_poll(&c,&snap)&&c.active&&!acks);io.regs[0x20/4]=0;assert(!vxl_v2_execution_snapshot(x,seq,&snap));}
 {int fail=(fault>=3&&fault!=7)||(!fast&&(fault==1||fault==2));int rc=vxl_v2_observe(&c,&snap,snapshot_rc);
  if(fail){assert(rc<0&&c.quarantined&&acks==(fault==8?1u:0u));assert(!vxl_v2_read_completion(&c,1,seq,1,&done)&&!done.cycles_valid);assert(vxl_v2_ack_reap(&c,1,seq,1)==-EIO);}
  else{assert(!rc&&!c.active&&acks==1);assert(!vxl_v2_read_completion(&c,1,seq,1,&done));assert(done.cycles_valid&&done.elapsed_raw==37);
   if(fast){assert(done.version==4&&done.reserved0==1&&done.digest_ns==0);assert(!source_reads&&!args_reads);assert(!snap.source_ok&&!snap.parameters_ok);}
   else assert(done.version==2&&source_reads>=3&&args_reads>=3&&done.reserved0==0);
   assert(!vxl_v2_ack_reap(&c,1,seq,1));}
 }
end:free(io.ps);free(table);free(q);free(x);
}
int main(int argc,char **argv){assert(argc==2);check(argv[1],1,0);check(argv[1],0,0);check(argv[1],0,1);check(argv[1],0,2);for(unsigned f=1;f<=9;f++)check(argv[1],1,f);puts("FAST_NATIVE_ZERO_CONTENT_READBACK_METADATA_GUARDS_RETIRE_DRAIN_ACK_STRICT_COMPAT_HOST_PASS");return 0;}

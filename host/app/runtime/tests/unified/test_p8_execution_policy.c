/* Actual native policy binding/snapshot functions, MMIO/CDMA host fixture.
 * No GPU execution, waveform, physical cycle or board acceptance. */
#define main provider_component_main
#include "../v2/test_provider.c"
#undef main
#include <vxl/v2/execution.h>
#include <vxl/v2/startup.h>
#include <vxl/hunet.h>
static vxl_u64 ns(void*p){return ++((struct hardware*)p)->ms*1000+1;}
static void need(int ok,const char*why){if(!ok){fprintf(stderr,"U16_POLICY_ASSERT %s\n",why);exit(1);}}
int main(int argc,char**argv){
 need(argc==4,"catalog first code and actual execution manifest");struct hardware io={.locked=1,.completed=1};struct vxl_v2_ctx c;struct vxl_v2_hw h={0};
 struct vxl_v2_catalog cat;vxl_u8 raw[16384],code[2048];FILE*f=fopen(argv[1],"rb");need(f!=0,"catalog open");size_t n=fread(raw,1,sizeof(raw),f);fclose(f);need(n>=64&&n<sizeof(raw),"catalog bound");
 io.ps=calloc(1,VXL_PS_SIZE);need(io.ps!=0,"private PS fixture");h.ctx=&c;h.identity=p8();h.ps=io.ps;h.ps_dma=VXL_PS_BASE;h.os=&io;h.locked=locked;h.ready=ready;h.read=read_reg;h.write=write_reg;h.memory_barrier=barrier;h.now_ns=ns;h.cdma=(struct vxl_cdma_io){&io,dma_read,dma_write,dma_now,dma_pause,0};io.dma[1]=2;io.regs[0]=0x56580204;io.regs[1]=0x40404;io.regs[0x34/4]=1;
 vxl_v2_init(&c,&vxl_v2_native_memory_ops,&h);need(!vxl_v2_catalog_open(&cat,raw,n,&h.identity.provider)&&cat.count==4,"actual P8 catalog");
 struct vxl_v2_execution_policy policy[4];memset(policy,0,sizeof(policy));
 for(unsigned i=0;i<4;i++){memcpy(policy[i].entry_id,raw+64+512*i+256,32);policy[i].watchdog=4000000000u;policy[i].retire_ref=VXL_V2_ABSENT;policy[i].directory_ref=VXL_V2_ABSENT;policy[i].indices_ref=VXL_V2_ABSENT;policy[i].reserved[0]=1;/* proposed generic GLOBAL_DRAIN, not a fabricated exit word */
  if(i&1){policy[i].directory_ref=3;policy[i].indices_ref=5;policy[i].faces_arg_offset=32;policy[i].tile_count=i==3?16384:4096;policy[i].max_faces=i==3?30926:768;policy[i].max_indices=262144;}
 }
 struct vxl_v2_execution*x=calloc(1,sizeof(*x));need(x!=0,"execution context");need(vxl_v2_execution_bind(x,&h,&cat,policy,4)==0,"native P8 global DONE and16384tile binding required");
 {struct {struct vxl_v2_execution_header header;struct vxl_v2_execution_policy policies[4];} manifest;
  vxl_u8 csha[32],psha[32];memset(&manifest,0,sizeof(manifest));manifest.header.magic=VXL_V2_EXEC_MAGIC;manifest.header.version=1;manifest.header.bytes=sizeof(manifest);manifest.header.count=4;memcpy(manifest.header.catalog_sha,cat.sha,32);memcpy(manifest.policies,policy,sizeof(policy));vxl_hunet_hash(raw,n,csha);
  io.regs[0x30/4]=15;io.regs[0x38/4]=0x10001;io.regs[0x60/4]=0x41584931;io.regs[0x64/4]=1;io.regs[0x68/4]=4;vxl_hunet_hash(&manifest,sizeof(manifest),psha);
  need(vxl_v2_boot_bind(x,&h,&cat,raw,n,&manifest,sizeof(manifest),csha,psha)==-EINVAL,"header1 cannot activate new retirement semantics");manifest.header.version=2;vxl_hunet_hash(&manifest,sizeof(manifest),psha);
  need(!vxl_v2_boot_bind(x,&h,&cat,raw,n,&manifest,sizeof(manifest),csha,psha),"explicit header2 binds actual global drain programs");
  unsigned char emitted[sizeof(manifest)];f=fopen(argv[3],"rb");need(f!=0,"emitted execution manifest open");need(fread(emitted,1,sizeof(emitted),f)==sizeof(emitted)&&fgetc(f)==EOF,"emitted execution manifest exact extent");fclose(f);
  need(!memcmp(emitted,&manifest,sizeof(manifest)),"actual emitted manifest equals frozen four program policies");vxl_hunet_hash(emitted,sizeof(emitted),psha);
  need(!vxl_v2_boot_bind(x,&h,&cat,raw,n,emitted,sizeof(emitted),csha,psha),"boot binder accepts actual generated execution artifact");emitted[sizeof(emitted)-1]^=1;
  need(vxl_v2_boot_bind(x,&h,&cat,raw,n,emitted,sizeof(emitted),csha,psha)!=0,"actual manifest mutation rejected before binding");

 }
 policy[3].tile_count=16385;need(vxl_v2_execution_bind(x,&h,&cat,policy,4)==-EINVAL,"tile bound remains finite");policy[3].tile_count=16384;policy[0].reserved[0]=2;need(vxl_v2_execution_bind(x,&h,&cat,policy,4)==-EINVAL,"unknown retirement mode refused");policy[0].reserved[0]=1;policy[0].retire_count=4;need(vxl_v2_execution_bind(x,&h,&cat,policy,4)==-EINVAL,"no fake per-core words with global proof");policy[0].retire_count=0;need(!vxl_v2_execution_bind(x,&h,&cat,policy,4),"restored exact policy");
 struct vxl_v2_program_table*table=calloc(1,sizeof(*table));need(table!=0,"table");need(!vxl_v2_table_bind(&c,table,&cat)&&!vxl_v2_owner_enter(&c,1,1),"shared private catalog binding");struct vxl_v2_catalog_entry e;need(!vxl_v2_catalog_lookup(&cat,raw+320,&e),"program9 entry");f=fopen(argv[2],"rb");need(f!=0,"code open");need(fread(code,1,sizeof(code),f)==sizeof(code)&&fgetc(f)==EOF,"exact original padded code");fclose(f);struct vxl_v2_program_ref program;need(!vxl_v2_program_load(&c,1,&e,code,0,&program),"native original code loader");
 struct vxl_v2_program*p=&table->programs[0];need(p->ref.handle==program.handle&&p->owned_count==2,"no invented CRT allocation");struct vxl_v2_job job={.major=2,.size=512,.device_generation=1,.args_bytes=64};
 x->program=p;x->job=&job;x->policy=policy[0];x->args=p->owned[1];x->args_bytes=64;memset(x->shadow,0,64);
 {struct vxl_v2_object*arg;need(vxl_v2_buffer_copy(&c,1,&x->args,x->shadow,1)!=0,"public COPY cannot overwrite pinned program-owned args");need(!vxl_v2_ref_resolve(&c,1,&x->args,1,&arg)&&!vxl_v2_native_memory_ops.copy(&h,arg,x->args.offset,x->shadow,64,1),"trusted locked native private args snapshot");}
 x->started=1;x->jobs_before=0;
 io.regs[0x10/4]=e.entry_pc;io.regs[0x14/4]=e.args_gpu;io.regs[0x18/4]=policy[0].watchdog;io.regs[0x28/4]=37;io.regs[0x2c/4]=1;io.regs[3]=0x502;
 struct vxl_v2_snapshot snap;need(!vxl_v2_execution_snapshot(x,1,&snap)&&x->verified&&snap.core_mask==15,"original DONE state with drain sources args guards checked");
 io.regs[3]=0x402;need(!vxl_v2_execution_snapshot(x,1,&snap)&&!x->verified,"DRAIN cannot impersonate DONE");io.regs[3]=0x502;io.regs[0x24/4]=1;need(!vxl_v2_execution_snapshot(x,1,&snap)&&!x->verified,"pending read invalidates old verified proof");io.regs[0x24/4]=0;io.regs[0x58/4]=1;need(!vxl_v2_execution_snapshot(x,1,&snap)&&!x->verified,"TEX pending invalidates old proof");io.regs[0x58/4]=0;
 need(!vxl_v2_execution_snapshot(x,1,&snap)&&x->verified,"fresh valid snapshot recomputes proof");
 need(!vxl_v2_program_release(&c,1,&program),"private program release");free(table);free(x);free(io.ps);puts("U16_NATIVE_GLOBAL_DONE_POLICY_HOST_REGISTER_FIXTURE_PASS_NOT_GPU_BOARD gpu_raw_cycles=null");return 0;
}

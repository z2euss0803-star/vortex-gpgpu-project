/* Actual shared C boot/catalog/execution binder, HOST registers only.
 * No GPU tasks, device nodes, hardware measurements or injected GPU output. */
#define main provider_component_main
#include "../v2/test_provider.c"
#undef main
#include <vxl/hunet.h>
#include <vxl/v2/execution.h>
#include <vxl/v2/startup.h>

static size_t input(const char *path,void *bytes,size_t capacity){
 FILE *f=fopen(path,"rb");size_t n;assert(f);n=fread(bytes,1,capacity,f);assert(!ferror(f)&&feof(f));fclose(f);return n;
}
int main(int argc,char **argv){
 struct hardware io={.locked=1};struct vxl_v2_hw h={0};
 struct vxl_v2_ctx *ctx=calloc(1,sizeof(*ctx));struct vxl_v2_execution *execution=calloc(1,sizeof(*execution));struct vxl_v2_catalog catalog;
 vxl_u8 *raw=malloc(65537),policy[4161],csha[32],psha[32];size_t n,m;int rc;unsigned provider;
 assert(argc==4&&ctx&&execution&&raw);provider=(unsigned)strtoul(argv[3],0,10);assert(provider==1||provider==2);
 n=input(argv[1],raw,65537);m=input(argv[2],policy,sizeof(policy));assert(n<=65536&&m<=4160);
 io.ps=calloc(1,VXL_PS_SIZE);assert(io.ps);io.dma[1]=2;io.regs[0]=0x56580204;io.regs[1]=0x40404;io.regs[0x30/4]=15;io.regs[0x34/4]=1;io.regs[0x38/4]=0x10001;
 io.regs[0x60/4]=0x41584931;io.regs[0x64/4]=1;io.regs[0x68/4]=4;
 h.ctx=ctx;h.identity=provider==1?s8():p8();h.ps=io.ps;h.ps_dma=VXL_PS_BASE;h.os=&io;h.locked=locked;h.ready=ready;h.read=read_reg;h.write=write_reg;h.memory_barrier=barrier;h.cdma=(struct vxl_cdma_io){&io,dma_read,dma_write,dma_now,dma_pause,0};
 vxl_v2_init(ctx,&vxl_v2_native_memory_ops,&h);ctx->guard_bytes=64;vxl_hunet_hash(raw,(vxl_u32)n,csha);vxl_hunet_hash(policy,(vxl_u32)m,psha);
 rc=vxl_v2_boot_bind(execution,&h,&catalog,raw,(vxl_u32)n,policy,(vxl_u32)m,csha,psha);
 if(rc){fprintf(stderr,"U23_PRODUCTION_BOOT_BIND_REJECTED provider=%u rc=%d\n",provider,rc);return 1;}
 assert(execution->count==(provider==1?13u:4u)&&catalog.count==execution->count);
 assert(!io.writes&&!ctx->active&&!ctx->owner&&!ctx->live&&!ctx->quarantined);
 /* Busy hardware is rejected before a job or register write can happen. */
 io.regs[3]=1;assert(vxl_v2_boot_bind(execution,&h,&catalog,raw,(vxl_u32)n,policy,(vxl_u32)m,csha,psha)==-EBUSY);io.regs[3]=0;
 /* Same integer program/profile on another physical provider cannot bind. */
 h.identity=provider==1?p8():s8();assert(vxl_v2_boot_bind(execution,&h,&catalog,raw,(vxl_u32)n,policy,(vxl_u32)m,csha,psha)<0);assert(!io.writes);
 free(io.ps);free(raw);free(execution);free(ctx);
 printf("U23_PRODUCTION_FIRMWARE_SHARED_C_BIND_PASS_HOST_REGISTERS_ONLY provider=%u gpu_raw_cycles=null\n",provider);return 0;
}

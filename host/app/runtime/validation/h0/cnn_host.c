/* CPU drives VIP/CDMA and compares golden files; it never writes GPU answers. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "host_platform.h"
#include "gpgpu_hw.h"
static const char *names[]={"x","wconv","bconv","z","r","p","wfc","bfc","y"};
static const unsigned counts[]={64,18,2,72,72,18,36,2,2};
static const char *cases[]={"main","zero","negative","positive","checker"};
static uint32_t initial[9][112],expected[9][112];static unsigned sizes[9];
static uint32_t rd(unsigned off){return host_mmio_read(GPU_BASE+off);}
static void wr(unsigned off,uint32_t v){host_mmio_write(GPU_BASE+off,v);host_barrier();}
static int fail(const char *w,unsigned i,unsigned v){host_log("CNN_FAIL %s index=%u value=%08x status=%08x errors=%08x jobs=%u pending=%u/%u\n",w,i,v,rd(GPU_STATUS),rd(GPU_ERRORS),rd(GPU_JOBS),rd(GPU_PENDING_READS),rd(GPU_PENDING_WRITES));return -1;}
static uint32_t gpu(unsigned o){return UINT32_C(0x80040000)+o*4096;}
static uint64_t bus(unsigned o){return PL_BASE+UINT64_C(0x40000)+o*4096;}
static int dma(uint64_t src,uint64_t dst,unsigned n){
 unsigned i,s=0;if((src|dst|n)&15 || !n || n>4096)return fail("DMA_ALIGNMENT",n,0);
 for(i=0;i<100000;i++){s=host_mmio_read(CDMA_BASE+CDMA_SR);if(cdma_status_error(s))return fail("DMA_PRIOR",i,s);if(s&CDMA_SR_IDLE)break;}
 if(i==100000)return fail("DMA_PRIOR_TIMEOUT",i,s);
 host_mmio_write(CDMA_BASE+CDMA_CR,0);host_mmio_write(CDMA_BASE+CDMA_SR,CDMA_SR_W1C_NORMAL);
 s=host_mmio_read(CDMA_BASE+CDMA_SR);if(cdma_status_error(s)||(s&CDMA_SR_W1C_NORMAL))return fail("DMA_STALE",0,s);
 host_mmio_write(CDMA_BASE+CDMA_SA_MSB,(uint32_t)(src>>32));host_mmio_write(CDMA_BASE+CDMA_SA,(uint32_t)src);
 host_mmio_write(CDMA_BASE+CDMA_DA_MSB,(uint32_t)(dst>>32));host_mmio_write(CDMA_BASE+CDMA_DA,(uint32_t)dst);host_barrier();host_mmio_write(CDMA_BASE+CDMA_BTT,n);
 for(i=0;i<100000;i++){s=host_mmio_read(CDMA_BASE+CDMA_SR);if(cdma_status_error(s))return fail("DMA",i,s);if(cdma_status_complete(s))return 0;}
 return fail("DMA_TIMEOUT",i,s);
}
static int load(const char *p,uint32_t *w,unsigned n){FILE *f=fopen(p,"rb");if(!f)return fail("FILE_OPEN",n,0);int ok=fread(w,1,n,f)==n&&fgetc(f)==EOF;fclose(f);return ok?0:fail("FILE_SIZE",n,0);}
static int upload(unsigned o){for(unsigned i=0;i<sizes[o]/4;i++)host_ps_write(PS_STAGE+4*i,initial[o][i]);host_flush(PS_STAGE,sizes[o]);host_barrier();return dma(PS_STAGE,bus(o),sizes[o]);}
static int compare(unsigned ci,unsigned id){
 for(unsigned o=0;o<9;o++){
  if(dma(bus(o),PS_STAGE,sizes[o]))return -1;host_invalidate(PS_STAGE,sizes[o]);host_barrier();uint32_t actual[112];
  int done=(o==3&&id>=2)||(o==4&&id>=3)||(o==5&&id>=4)||(o==8&&id>=5);const uint32_t *want=done?expected[o]:initial[o];
  for(unsigned i=0;i<sizes[o]/4;i++){actual[i]=host_ps_read(PS_STAGE+4*i);if(actual[i]!=want[i])return fail("OBJECT_GOLDEN_OR_GUARD",i,actual[i]);}
  char path[200];snprintf(path,sizeof(path),"outputs/%s-id%u-%s.bin",cases[ci],id,names[o]);FILE *f=fopen(path,"wb");if(!f)return fail("OUTPUT_OPEN",o,0);
  int ok=fwrite(actual,1,sizes[o],f)==sizes[o]&&fflush(f)==0;int rc=fclose(f);if(!ok||rc)return fail("OUTPUT_WRITE",o,0);
 }return 0;
}
static int launch(unsigned ci,unsigned id){
 uint32_t code[1024],args[16]={0x434e4e31,64,1,0};char path[100];snprintf(path,sizeof(path),"programs/id%u.bin",id);if(load(path,code,4096))return -1;
 for(unsigned i=0;i<1024;i++)host_ps_write(PS_CODE+4*i,code[i]);host_flush(PS_CODE,4096);host_barrier();
 const unsigned map[4][4]={{0,1,2,3},{3,9,9,4},{4,9,9,5},{5,6,7,8}};
 args[3]=id;for(unsigned i=0;i<4;i++){unsigned o=map[id-2][i];if(o<9){args[4+i]=gpu(o)+64;args[8+i]=counts[o];}}
 for(unsigned i=0;i<16;i++)host_ps_write(PS_ARGS+4*i,args[i]);host_flush(PS_ARGS,64);host_barrier();
 if(rd(GPU_STATUS)!=0)return fail("NOT_IDLE",id,rd(GPU_STATUS));unsigned before=rd(GPU_JOBS),s=0,i;
 host_phase(4);wr(GPU_PC,(uint32_t)PS_CODE);wr(GPU_ARG,(uint32_t)PS_ARGS);wr(GPU_WATCHDOG,100000000);wr(GPU_TEX_MODE,0);wr(GPU_COMMAND,1);
 for(i=0;i<200000;i++){s=rd(GPU_STATUS);if((s&GPU_STATUS_ERROR)||((s>>8)&7)==GPU_STATE_FAULT)return fail("GPU_FAULT",id,s);if(s&GPU_STATUS_DONE)break;}
 if(i==200000)return fail("GPU_TIMEOUT",id,s);
 if(rd(GPU_ERRORS)||rd(GPU_PENDING_WRITES)||rd(GPU_PENDING_READS)||rd(GPU_JOBS)!=before+1)return fail("COMPLETION",id,s);
 host_invalidate(PS_FLUSH_LINE,64);for(i=0;i<16;i++)if(host_ps_read(PS_FLUSH_LINE+4*i)!=0xf1a50000+i)return fail("FENCE_LINE",i,0);
 if(compare(ci,id))return -1;
 host_log("CNN_LAYER_PASS case=%s id=%u output_elements=%u objects=9 guards=PASS jobs=%u fence=PASS\n",cases[ci],id,counts[map[id-2][3]],rd(GPU_JOBS));
 wr(GPU_COMMAND,2);host_phase(3);return rd(GPU_STATUS)==0?0:fail("ACK_NOT_IDLE",id,rd(GPU_STATUS));
}
int cnn_rtl_host_run(void){
 unsigned i;host_phase(1);for(i=0;i<100000;i++)if((rd(GPU_MEMORY_READY)&1)&&(host_mmio_read(READY_GPIO_BASE)&1))break;if(i==100000)return fail("READY",i,0);
 if(rd(GPU_ID)!=0x56580204||rd(GPU_CONFIG)!=0x00040404)return fail("IDENTITY",0,rd(GPU_CONFIG));
 for(i=0;i<16;i++)host_ps_write(PS_FLUSH_LINE+4*i,0xf1a50000+i);host_flush(PS_FLUSH_LINE,64);
 for(unsigned ci=0;ci<5;ci++){
  for(unsigned o=0;o<9;o++){sizes[o]=128+((counts[o]*4+63)&~63);char path[200];snprintf(path,sizeof(path),"fixtures/%s/%s.bin",cases[ci],names[o]);if(load(path,initial[o],sizes[o]))return -1;
   snprintf(path,sizeof(path),"fixtures/%s/%s%s.bin",cases[ci],names[o],(o==3||o==4||o==5||o==8)?".expected":"");if(load(path,expected[o],sizes[o]))return -1;if(upload(o))return -1;}
  for(unsigned id=2;id<=5;id++)if(launch(ci,id))return -1;host_log("CNN_CHAIN_PASS case=%s\n",cases[ci]);
 }host_log("CNN_HOST_COMPLETE launches=20 golden_only_compared=1\n");return 0;
}

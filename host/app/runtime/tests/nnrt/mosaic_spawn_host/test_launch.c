/* Link the actual SDK vx_spawn.c and the launch/owner expressions extracted
 * from the real Mosaic entry. CSR and WSPAWN shims replay physical SIMT lanes;
 * this is neither a GPU instruction/cycle model nor a barrier/memory model.
 * No test manually calls the arithmetic primitive for core0..3. */
#include <vx_spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int vxm_host_launch(void);
static unsigned host_core,host_warp,host_lane;
static uintptr_t scratch;
static unsigned callbacks,owners[4][4],bad,first,count;
static unsigned scalar[4096],pe[4096][4];
unsigned vx_num_threads(void){return 4;}
unsigned vx_num_warps(void){return 4;}
unsigned vx_num_cores(void){return 4;}
unsigned vx_core_id(void){return host_core;}
unsigned vx_warp_id(void){return host_warp;}
unsigned vx_thread_id(void){return host_lane;}
unsigned vx_hart_id(void){return host_core*16+host_warp*4+host_lane;}
uintptr_t vxm_host_csr_read(unsigned csr){if(csr!=VX_CSR_MSCRATCH)abort();return scratch;}
void vxm_host_csr_write(unsigned csr,uintptr_t value){if(csr!=VX_CSR_MSCRATCH)abort();scratch=value;}
void vx_tmc(unsigned mask){(void)mask;}
void vx_tmc_zero(void){}
void vx_tmc_one(void){}
void vx_wspawn(unsigned n,void (*entry)(void)){
 unsigned saved=host_warp;
 if(entry)for(host_warp=1;host_warp<n;++host_warp)entry();
 host_warp=saved;
}
int vx_printf(const char *fmt,...){va_list ap;va_start(ap,fmt);int rc=vfprintf(stderr,fmt,ap);va_end(ap);return rc;}
void vxm_host_trace_enter(void){++callbacks;}
void vxm_host_trace_owner(unsigned core,unsigned warp,unsigned lane,int rejected){
 if(rejected||core>3||warp||lane>3){++bad;return;}
 ++owners[core][lane];
 // Same global-item ownership contract as Quant/Conv/pointwise primitives.
 for(unsigned offset=0;offset<count;++offset)if((first+offset)%4==core){
  if(!lane)++scalar[offset];
  ++pe[offset][lane];
 }
}
int main(void){
 const unsigned starts[]={0,1,2,3,63,255,4096,610304};
 const unsigned counts[]={1,64,256,4096};
 unsigned cases=0,failures=0,total_items=0;
 for(unsigned a=0;a<sizeof(starts)/sizeof(starts[0]);++a)
  for(unsigned b=0;b<sizeof(counts)/sizeof(counts[0]);++b){
   first=starts[a];count=counts[b];callbacks=bad=0;
   memset(owners,0,sizeof(owners));memset(scalar,0,sizeof(scalar));memset(pe,0,sizeof(pe));
   /* The CRT starts each core. Each replay follows the real scheduler with
    * one physical lane selected; WSPAWN replays that lane on spawned warps.
    * Both launch shapes under test contain complete warps, so TMC masking
    * requires no partial-warp model. */
   for(host_core=0;host_core<4;++host_core)for(host_lane=0;host_lane<4;++host_lane){
    host_warp=0;scratch=0;
    int rc=vxm_host_launch();
    if(rc){fprintf(stderr,"SPAWN_ERROR core=%u rc=%d\n",host_core,rc);return 2;}
   }
   unsigned owner_cores=0,owner_lanes=0;
   for(unsigned core=0;core<4;++core){
    unsigned lanes=0;
    for(unsigned lane=0;lane<4;++lane){lanes+=owners[core][lane];if(owners[core][lane]!=1)++bad;}
    owner_lanes+=lanes;if(lanes)++owner_cores;
   }
   unsigned covered=0,duplicates=0,pe_missing=0;
   for(unsigned i=0;i<count;++i){
    covered+=scalar[i]!=0;duplicates+=scalar[i]>1;
    for(unsigned lane=0;lane<4;++lane){pe_missing+=pe[i][lane]==0;duplicates+=pe[i][lane]>1;}
   }
   int pass=bad==0&&owner_cores==4&&owner_lanes==16&&covered==count&&!duplicates&&!pe_missing;
   printf("SDK_LAUNCH first=%u count=%u callbacks=%u owner_cores=%u owner_lanes=%u scalar_items=%u missing=%u pe_missing=%u duplicates=%u bad_owner=%u PASS=%d\n",
    first,count,callbacks,owner_cores,owner_lanes,covered,count-covered,pe_missing,duplicates,bad,pass);
   ++cases;failures+=!pass;total_items+=count;
  }
 printf("R5_MOSAIC_ACTUAL_SDK_LAUNCH_%s_HOST_ONLY_NOT_GPU cases=%u items=%u failures=%u\n",
  failures?"FAIL":"PASS",cases,total_items,failures);
 return failures?1:0;
}

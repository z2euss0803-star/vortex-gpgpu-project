#include <stddef.h>
#include <string.h>
#include "image_pipeline.h"
#include "host_platform.h"
#include "generated/tables.h"
static image_execute_fn run_fn;static image_present_fn present_fn;static void *context;
static uint32_t width=IMAGE_WIDTH,height=IMAGE_HEIGHT,epoch,active_id;static image_rect rect;
typedef struct {uint32_t valid,epoch,crc;image_params p;} cache_slot;
static cache_slot cache[22];static unsigned smooth_valid,neon_valid,mean_block;
static unsigned cache_index(const image_params *p){return p->style==1?p->preset:p->style==2?7u:p->style==3?8u+p->preset:15u+p->preset;}
static unsigned result_slot(const image_params *p){return !p->preset?p->style+1u:p->style==1?10u+p->preset:p->style==3?16u+p->preset:22u+p->preset;}
static uint32_t bytes(void){return width*height*4u;}
static uint32_t addr(unsigned slot){return IMAGE_SLOT_BASE+slot*IMAGE_SLOT_SIZE+64u;}
static uint32_t guard(unsigned slot,unsigned side,unsigned i){return 0x69a50000u^(slot*0x010301u)^(side*0x10000u)^(i*0x01010307u);}
static uint32_t slot_bytes(unsigned slot){return slot==10u?65536u:bytes();}
static void init_guards(void){for(unsigned slot=0;slot<IMAGE_SLOT_COUNT;slot++){uint32_t base=addr(slot),n=slot_bytes(slot);for(unsigned i=0;i<16;i++){host_ps_write(base-64u+4u*i,guard(slot,0,i));host_ps_write(base+n+4u*i,guard(slot,1,i));}host_flush(base-64u,64);host_flush(base+n,64);}host_barrier();}
int image_pipeline_guards_check(void){for(unsigned slot=0;slot<IMAGE_SLOT_COUNT;slot++){uint32_t base=addr(slot),n=slot_bytes(slot);host_invalidate(base-64u,64);host_invalidate(base+n,64);for(unsigned i=0;i<16;i++)if(host_ps_read(base-64u+4u*i)!=guard(slot,0,i)||host_ps_read(base+n+4u*i)!=guard(slot,1,i))return -7;}return 0;}
static void table_copy(uint32_t target,const uint32_t *p,unsigned n){for(unsigned i=0;i<n;i++)host_ps_write(target+4u*i,p[i]);}
int image_pipeline_init(image_execute_fn run,image_present_fn present,void *ctx){
 if(!run||!present)return -4;run_fn=run;present_fn=present;context=ctx;epoch=active_id=0;memset(cache,0,sizeof(cache));smooth_valid=neon_valid=mean_block=0;width=IMAGE_WIDTH;height=IMAGE_HEIGHT;init_guards();
 table_copy(IMAGE_PAL_Y,img_palette_y,1792);table_copy(IMAGE_PAL_8,img_palette8,56);table_copy(IMAGE_THEME,img_themes,56);table_copy(IMAGE_BAYER,img_bayer,16);table_copy(IMAGE_GAUSS7,img_gauss7,7);table_copy(IMAGE_GAUSS9,img_gauss9,9);table_copy(IMAGE_XDOG_TABLE,img_xdog,4096);host_flush(IMAGE_TABLE,65536);host_barrier();return image_pipeline_guards_check();
}
int image_pipeline_configure(uint32_t w,uint32_t h){if(active_id||!((w==1280&&h==720)||(w==256&&h==144)))return -4;width=w;height=h;init_guards();return 0;}
uint32_t image_pipeline_input_id(void){return active_id;}
int image_pipeline_commit(const uint8_t *rgba,const image_rect *r,uint32_t id,uint32_t crc){
 if(!rgba||!r||!id||id<=active_id||epoch==UINT32_MAX||!r->w||!r->h||r->x>=width||r->y>=height||r->w>width-r->x||r->h>height-r->y)return -4;
 if(image_pipeline_guards_check())return -7;if(image_crc32(rgba,bytes())!=crc)return -1;
 for(uint32_t i=0;i<bytes();i+=4){uint32_t v;memcpy(&v,rgba+i,4);if((v>>24)!=255)return -4;}
 for(uint32_t i=0;i<bytes();i+=4){uint32_t v;memcpy(&v,rgba+i,4);host_ps_write(IMAGE_ACTIVE+i,v);if(!(i&16383u))host_service_poll();}
 host_flush(IMAGE_ACTIVE,bytes());host_barrier();rect=*r;active_id=id;epoch++;memset(cache,0,sizeof(cache));smooth_valid=neon_valid=mean_block=0;return image_pipeline_guards_check();
}
static int dispatch(enum image_op op,image_job *j,image_result *r){image_gpu_stats stats={0};int rc=run_fn(context,op,j,&stats);if(rc)return rc;
 if(stats.errors||stats.pending_rd||stats.pending_wr||stats.jobs_after!=stats.jobs_before+1u)return -6;r->pass_count++;r->gpu_cycles+=stats.cycles;return image_pipeline_guards_check();}
static uint32_t scratch(unsigned i){return addr(i+6);}
static int parameters(const image_params *p){return p&&p->style<=4&&p->preset<=6&&(!(p->style==0||p->style==2)||p->preset==0)&&(p->style==4?(p->block==2||p->block==4||p->block==8):p->block==0);}
/* Cached intermediates become valid only after each complete drained pass chain. */
static int smooth(image_job *j,image_result *r){
 if(smooth_valid)return 0;int rc;j->src0=IMAGE_ACTIVE;j->dst=scratch(0);
 if((rc=dispatch(IMAGE_GRAY,j,r)))return rc;
 for(unsigned i=0;i<3;i++){j->src0=scratch(i%2);j->dst=i==2?IMAGE_SMOOTH:scratch((i+1)%2);if((rc=dispatch(IMAGE_BLUR3,j,r)))return rc;}
 smooth_valid=1;return 0;
}
int image_pipeline_render(const image_params *p,image_result *r){
 if(!run_fn||!active_id||!r||!parameters(p))return -4;if(image_pipeline_guards_check())return -7;
 *r=(image_result){.input_id=active_id,.epoch=epoch,.style=p->style,.preset=p->preset,.block=p->block};
 uint32_t dst=p->style?addr(result_slot(p)):IMAGE_ACTIVE;cache_slot *c=p->style?cache+cache_index(p):NULL;
 if(c&&c->valid&&c->epoch==epoch&&c->p.style==p->style&&c->p.preset==p->preset&&c->p.block==p->block){r->cache_hit=1;r->output_crc=c->crc;}
 else if(!p->style){host_invalidate(dst,bytes());r->output_crc=image_crc_region(dst,bytes());}
 else{
  c->valid=0;uint64_t start=image_ticks();image_job j={.src0=IMAGE_ACTIVE,.dst=scratch(0),.width=width,.height=height,.rect_x=rect.x,.rect_y=rect.y,.rect_w=rect.w,.rect_h=rect.h,.flags=p->style==2?1u:0u};int rc;
  if((p->style==1||p->style==3)&&(rc=smooth(&j,r)))return rc;
  if(p->style==1){j.src0=IMAGE_SMOOTH;j.dst=dst;j.table=IMAGE_PAL_Y+p->preset*1024u;j.param0=img_themes[p->preset*8u+4u];if((rc=dispatch(IMAGE_SOBEL_POSTER,&j,r)))return rc;}
  else if(p->style==2){
   if((rc=dispatch(IMAGE_GRAY,&j,r)))return rc;
   for(unsigned g=0;g<2;g++){j.src0=scratch(0);j.dst=scratch(1);j.table=g?IMAGE_GAUSS9:IMAGE_GAUSS7;j.param0=g?4:3;j.param1=g?9:7;if((rc=dispatch(IMAGE_GAUSSIAN_H,&j,r)))return rc;j.src0=scratch(1);j.dst=scratch(g?3:2);if((rc=dispatch(IMAGE_GAUSSIAN_V,&j,r)))return rc;}j.src0=scratch(2);j.src1=scratch(3);j.dst=dst;j.table=IMAGE_XDOG_TABLE;if((rc=dispatch(IMAGE_XDOG_COLOR,&j,r)))return rc;
  }else if(p->style==3){
   if(!neon_valid){
    j.src0=IMAGE_SMOOTH;j.dst=IMAGE_CORE;if((rc=dispatch(IMAGE_NEON_CORE,&j,r)))return rc;
    for(unsigned i=0;i<5;i++){j.src0=i?scratch(i%2?0:3):IMAGE_CORE;j.dst=i==4?IMAGE_HALO:scratch(i%2?3:0);if((rc=dispatch(IMAGE_BLUR3,&j,r)))return rc;}
    neon_valid=1;
   }
   j.src0=IMAGE_SMOOTH;j.src1=IMAGE_CORE;j.src2=IMAGE_HALO;j.dst=dst;j.table=IMAGE_THEME+p->preset*32u;if((rc=dispatch(IMAGE_NEON_COMBINE,&j,r)))return rc;
  }else{
   j.param0=p->block;
   if(mean_block!=p->block){j.src0=IMAGE_ACTIVE;j.dst=IMAGE_MEAN;if((rc=dispatch(IMAGE_PIXEL_MEAN,&j,r)))return rc;mean_block=p->block;}
   j.src0=IMAGE_MEAN;j.src1=IMAGE_BAYER;j.dst=dst;j.table=IMAGE_PAL_8+p->preset*32u;if((rc=dispatch(IMAGE_PIXEL_BAYER,&j,r)))return rc;
  }
  r->generate_ticks=image_ticks()-start;host_invalidate(dst,bytes());host_barrier();r->output_crc=image_crc_region(dst,bytes());
 }
 int rc=present_fn(context,(const uint32_t*)(uintptr_t)dst,width,height,&r->blit_ticks,&r->retire_ticks);if(rc)return rc;
 if(image_pipeline_guards_check())return -7;
 if(c){c->valid=1;c->epoch=epoch;c->p=*p;c->crc=r->output_crc;}return 0;
}

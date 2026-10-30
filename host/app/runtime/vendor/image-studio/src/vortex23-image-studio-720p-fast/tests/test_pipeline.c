#include <assert.h>
#include <stdio.h>
#include "image_pipeline.h"
#include "rv32_image_model.h"
#include "generated/image_kernels.h"
static unsigned calls,presents,fail_at;static uint32_t previous_crc,displayed;
uint32_t host_ps_read(uint64_t a){assert(a>=IMAGE_SLOT_BASE&&a<IMAGE_SLOT_BASE+IMAGE_SLOT_COUNT*IMAGE_SLOT_SIZE&&!(a&3u));return im_memory[(a-IMAGE_SLOT_BASE)/4u];}
void host_ps_write(uint64_t a,uint32_t v){assert(a>=IMAGE_SLOT_BASE&&a<IMAGE_SLOT_BASE+IMAGE_SLOT_COUNT*IMAGE_SLOT_SIZE&&!(a&3u));im_memory[(a-IMAGE_SLOT_BASE)/4u]=v;}
void host_flush(uint64_t a,uint32_t n){(void)a;(void)n;}
void host_invalidate(uint64_t a,uint32_t n){(void)a;(void)n;}
void host_barrier(void){}
void host_service_poll(void){}
uint64_t image_ticks(void){static uint64_t t;return ++t;}
static int execute(void *ctx,enum image_op op,const image_job *j,image_gpu_stats *stats){
 (void)ctx;calls++;if(fail_at&&calls==fail_at)return -6;
 uint32_t count=j->width*j->height;if(op==IMAGE_PIXEL_MEAN)count/=j->param0*j->param0;
 int rc=im_run(img_kernels[op],img_kernel_words[op],j,count);assert(!rc);
 *stats=(image_gpu_stats){.jobs_before=calls-1,.jobs_after=calls};return rc;
}
static int present(void *ctx,const uint32_t *p,uint32_t w,uint32_t h,uint64_t *blit,uint64_t *wait){
 (void)ctx;displayed=(uint32_t)(uintptr_t)p;presents++;*blit=1;*wait=2;previous_crc=image_crc_region((uint32_t)(uintptr_t)p,w*h*4u);return 0;
}
int main(int argc,char **argv){
 assert(argc==3);im_memory=calloc(IMAGE_SLOT_COUNT*IMAGE_SLOT_SIZE/4u,4);assert(im_memory);
 uint8_t *source=malloc(IMAGE_BYTES);FILE *f=fopen(argv[1],"rb");assert(f&&fread(source,1,IMAGE_BYTES,f)==IMAGE_BYTES);fclose(f);
 assert(!image_pipeline_init(execute,present,NULL));image_rect rect={0,0,1280,720};uint32_t crc=image_crc32(source,IMAGE_BYTES);
 assert(!image_pipeline_commit(source,&rect,1,crc));
 for(unsigned style=1;style<=4;style++){
  image_params p={style,(style==1||style==4)?1:0,style==4?4:0};image_result a,b;
  unsigned before=calls;assert(!image_pipeline_render(&p,&a));assert(a.pass_count==(style==1?5u:style==2?6u:style==3?7u:2u));
  assert(calls==before+a.pass_count&&a.output_crc==previous_crc);
  char path[1024];snprintf(path,sizeof(path),"%s/style-%u.rgba",argv[2],style);f=fopen(path,"wb");assert(f&&fwrite((uint8_t*)im_memory+displayed-IMAGE_SLOT_BASE,1,IMAGE_BYTES,f)==IMAGE_BYTES);fclose(f);
  before=calls;assert(!image_pipeline_render(&p,&b));assert(b.cache_hit&&b.pass_count==0&&calls==before&&b.output_crc==a.output_crc);
 }
 assert(!image_pipeline_commit(source,&rect,2,crc));image_params p={1,1,0};image_result a;unsigned before=calls;
 assert(!image_pipeline_render(&p,&a)&&!a.cache_hit&&calls==before+5);
 p.preset=0;fail_at=calls+1;unsigned shown=presents;uint32_t old=previous_crc;
 assert(image_pipeline_render(&p,&a)==-6&&presents==shown&&previous_crc==old);
 fail_at=0;assert(!image_pipeline_render(&p,&a)&&!a.cache_hit);
 assert(image_pipeline_commit(source,&rect,3,crc^1u)!=0);assert(!image_pipeline_guards_check());
 printf("IMAGE_PIPELINE_CACHE_GUARD_PASS calls=%u presents=%u\n",calls,presents);free(source);free(im_memory);return 0;
}

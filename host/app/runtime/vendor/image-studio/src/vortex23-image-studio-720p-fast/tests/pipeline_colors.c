#include <assert.h>
#include <stdio.h>
#include "image_pipeline.h"
#include "rv32_image_model.h"
#include "generated/image_kernels.h"
static unsigned jobs,fail_present;
uint32_t host_ps_read(uint64_t a){assert(a>=IMAGE_SLOT_BASE&&a<IMAGE_SLOT_BASE+IMAGE_SLOT_COUNT*IMAGE_SLOT_SIZE&&!(a&3));return im_memory[(a-IMAGE_SLOT_BASE)/4];}
void host_ps_write(uint64_t a,uint32_t v){assert(a>=IMAGE_SLOT_BASE&&a<IMAGE_SLOT_BASE+IMAGE_SLOT_COUNT*IMAGE_SLOT_SIZE&&!(a&3));im_memory[(a-IMAGE_SLOT_BASE)/4]=v;}
void host_flush(uint64_t a,uint32_t n){(void)a;(void)n;}void host_invalidate(uint64_t a,uint32_t n){(void)a;(void)n;}void host_barrier(void){}void host_service_poll(void){}
uint64_t image_ticks(void){static uint64_t t;return ++t;}
static int execute(void *ctx,enum image_op op,const image_job *j,image_gpu_stats *s){
 (void)ctx;unsigned n=j->width*j->height;if(op==IMAGE_PIXEL_MEAN)n/=j->param0*j->param0;
 assert(!im_run(img_kernels[op],img_kernel_words[op],j,n));*s=(image_gpu_stats){.jobs_before=jobs,.jobs_after=jobs+1};jobs++;return 0;
}
static uint32_t displayed;
static int present(void *ctx,const uint32_t *p,uint32_t w,uint32_t h,uint64_t *b,uint64_t *r){(void)ctx;assert(w==256&&h==144);if(fail_present)return -48;displayed=(uint32_t)(uintptr_t)p;*b=1;*r=2;return 0;}
int main(int argc,char **argv){
 assert(argc==3);im_memory=calloc(IMAGE_SLOT_COUNT*IMAGE_SLOT_SIZE/4,4);assert(im_memory);uint8_t source[256*144*4];FILE *f=fopen(argv[1],"rb");assert(f&&fread(source,1,sizeof(source),f)==sizeof(source));fclose(f);
 assert(!image_pipeline_init(execute,present,NULL));assert(!image_pipeline_configure(256,144));image_rect rect={3,4,247,131};uint32_t crc=image_crc32(source,sizeof(source));assert(!image_pipeline_commit(source,&rect,1,crc));
 const unsigned styles[]={3,2,1,4};uint32_t neon_crc=0;
 for(unsigned k=0;k<4;k++)for(unsigned p=0;p<(styles[k]==2?1u:7u);p++){
  unsigned style=styles[k],before=jobs;image_params params={style,p,style==4?8:0};image_result result,cached;
  assert(!image_pipeline_render(&params,&result));assert(!result.cache_hit&&result.pass_count==(style==1?1u:style==2?6u:style==3?(p?1u:11u):(p?1u:2u)));
  assert(!image_pipeline_render(&params,&cached));assert(cached.cache_hit&&cached.pass_count==0&&jobs==before+result.pass_count&&cached.output_crc==result.output_crc);
  char path[1024];snprintf(path,sizeof(path),"%s/style-%u-p%u.rgba",argv[2],style,p);f=fopen(path,"wb");assert(f&&fwrite((uint8_t*)im_memory+displayed-IMAGE_SLOT_BASE,1,sizeof(source),f)==sizeof(source));fclose(f);
  if(style==3&&p==6)neon_crc=result.output_crc;
 }
 /* All 22 prior themes must remain cached across other styles. */
 for(unsigned k=0;k<4;k++)for(unsigned p=0;p<(styles[k]==2?1u:7u);p++){
  image_params params={styles[k],p,styles[k]==4?8:0};image_result cached;unsigned before=jobs;
  assert(!image_pipeline_render(&params,&cached)&&cached.cache_hit&&cached.pass_count==0&&jobs==before);
 }
 for(unsigned block=2;block<=4;block+=2)for(unsigned p=0;p<7;p++){
  image_params params={4,p,block};image_result r;assert(!image_pipeline_render(&params,&r)&&!r.cache_hit&&r.pass_count==(p?1u:2u));
  char path[1024];snprintf(path,sizeof(path),"%s/style-4-p%u-b%u.rgba",argv[2],p,block);f=fopen(path,"wb");assert(f&&fwrite((uint8_t*)im_memory+displayed-IMAGE_SLOT_BASE,1,sizeof(source),f)==sizeof(source));fclose(f);
 }
 image_params mixed={4,0,2};image_result result;assert(!image_pipeline_render(&mixed,&result)&&result.pass_count==2);
 mixed=(image_params){4,1,4};assert(!image_pipeline_render(&mixed,&result)&&result.cache_hit);
 mixed=(image_params){4,2,2};assert(!image_pipeline_render(&mixed,&result)&&result.pass_count==1); /* cache hit above must not change current mean block */
 /* Failed presentation must not validate a result, but completed intermediates remain reusable. */
 assert(!image_pipeline_commit(source,&rect,2,crc));image_params poster={1,3,0};unsigned previous=displayed;fail_present=1;
 assert(image_pipeline_render(&poster,&result)==-48&&result.pass_count==5&&displayed==previous);uint32_t failed_crc=result.output_crc;fail_present=0;
 assert(!image_pipeline_render(&poster,&result)&&!result.cache_hit&&result.pass_count==1&&result.output_crc==failed_crc);
 assert(!image_pipeline_render(&poster,&result)&&result.cache_hit);
 image_params neon={3,6,0};assert(!image_pipeline_render(&neon,&result)&&!result.cache_hit&&result.pass_count==7&&result.output_crc==neon_crc);assert(!image_pipeline_guards_check());
 puts("REAL_C_PIPELINE_22_COLOR_OUTPUTS_CACHE_ORDER_PASS board_run=0");free(im_memory);return 0;
}

#include <assert.h>
#include <stdio.h>
#include "image_pipeline.h"
#include "rv32_image_model.h"
#include "generated/image_kernels.h"
static unsigned jobs;
uint32_t host_ps_read(uint64_t a){assert(a>=IMAGE_SLOT_BASE&&a<IMAGE_SLOT_BASE+IMAGE_SLOT_COUNT*IMAGE_SLOT_SIZE&&!(a&3));return im_memory[(a-IMAGE_SLOT_BASE)/4];}
void host_ps_write(uint64_t a,uint32_t v){assert(a>=IMAGE_SLOT_BASE&&a<IMAGE_SLOT_BASE+IMAGE_SLOT_COUNT*IMAGE_SLOT_SIZE&&!(a&3));im_memory[(a-IMAGE_SLOT_BASE)/4]=v;}
void host_flush(uint64_t a,uint32_t n){(void)a;(void)n;}void host_invalidate(uint64_t a,uint32_t n){(void)a;(void)n;}void host_barrier(void){}void host_service_poll(void){}
uint64_t image_ticks(void){static uint64_t t;return ++t;}
static int execute(void *ctx,enum image_op op,const image_job *j,image_gpu_stats *s){
 (void)ctx;unsigned n=j->width*j->height;if(op==IMAGE_PIXEL_MEAN)n/=j->param0*j->param0;
 assert(!im_run(img_kernels[op],img_kernel_words[op],j,n));*s=(image_gpu_stats){.jobs_before=jobs,.jobs_after=jobs+1};jobs++;return 0;
}
static uint32_t displayed;
static int present(void *ctx,const uint32_t *p,uint32_t w,uint32_t h,uint64_t *b,uint64_t *r){(void)ctx;assert(w==256&&h==144);displayed=(uint32_t)(uintptr_t)p;*b=1;*r=2;return 0;}
int main(int argc,char **argv){
 assert(argc==4);im_memory=calloc(IMAGE_SLOT_COUNT*IMAGE_SLOT_SIZE/4,4);assert(im_memory);
 uint8_t base[256*144*4],source[sizeof(base)];FILE *f=fopen(argv[1],"rb");assert(f&&fread(base,1,sizeof(base),f)==sizeof(base));fclose(f);
 assert(!image_pipeline_init(execute,present,NULL));assert(!image_pipeline_configure(256,144));image_rect rect={3,4,247,131};
 FILE *requests=fopen(argv[2],"r");assert(requests);unsigned tx,id,style,preset,block,active=0,hits=0,count=0;
 while(fscanf(requests,"%u %u %u %u %u",&tx,&id,&style,&preset,&block)==5){
  if(id!=active){memcpy(source,base,sizeof(base));for(unsigned i=0;i<sizeof(base);i+=4){unsigned x=(i/4u)%256u,y=i/(4u*256u);if(x>=3&&x<250&&y>=4&&y<135)source[i]^=(uint8_t)(id*17u);else{source[i]=7;source[i+1]=16;source[i+2]=30;}}assert(!image_pipeline_commit(source,&rect,id,image_crc32(source,sizeof(source))));active=id;}
  image_params p={style,preset,block};image_result r;assert(!image_pipeline_render(&p,&r));hits+=r.cache_hit;count++;
  char path[1024];snprintf(path,sizeof(path),"%s/tx-%u.rgba",argv[3],tx);f=fopen(path,"wb");assert(f&&fwrite((uint8_t*)im_memory+displayed-IMAGE_SLOT_BASE,1,sizeof(base),f)==sizeof(base));fclose(f);
  printf("REPLAY tx=%u passes=%u hit=%u crc=%08x\n",tx,r.pass_count,r.cache_hit,r.output_crc);
 }
 fclose(requests);assert(count==51&&jobs==87&&hits==10);assert(!image_pipeline_guards_check());puts("ACTUAL_C_RV32_REPLAY_51_REQUESTS_87_JOBS_PASS");free(im_memory);return 0;
}

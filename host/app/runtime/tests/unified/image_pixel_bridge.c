/* HOST arithmetic/recipe composition only. No MMIO, DMA, GPU or display. */
#include "image/pipeline_linux.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#include "vendor/image-studio/src/vortex23-image-studio-720p-fast/tests/rv32_image_model.h"
#pragma GCC diagnostic pop
#include "vendor/image-studio/src/vortex23-image-studio-720p-fast/generated/image_kernels.h"
extern uint32_t image_crc32(const uint8_t*,uint32_t);
static uint32_t width,height,jobs,hits;
uint32_t host_ps_read(uint64_t a){assert(a>=IMAGE_SLOT_BASE&&a<IMAGE_SLOT_BASE+VX_IMAGE_RESERVED_BYTES&&!(a&3));return im_memory[(a-IMAGE_SLOT_BASE)/4];}
static uint32_t guard(unsigned slot,unsigned side,unsigned i){return 0x69a50000u^(slot*0x010301u)^(side*0x10000u)^(i*0x01010307u);}
static void guards(int initialize){
 for(unsigned slot=0;slot<33;slot++){
  uint32_t n=slot==10?65536:width*height*4,offset=slot*IMAGE_SLOT_SIZE;
  for(unsigned side=0;side<2;side++)for(unsigned i=0;i<16;i++){
   uint32_t *p=im_memory+(offset+(side?64+n:0)+4*i)/4;
   if(initialize)*p=guard(slot,side,i);else assert(*p==guard(slot,side,i));
  }
 }
}
static void read_exact(const char*path,void*bytes,size_t n){FILE*f=fopen(path,"rb");assert(f);assert(fread(bytes,1,n,f)==n&&fgetc(f)==EOF);fclose(f);}
static void commit(vx_image_state*s,const image_rect*r,uint32_t id,const uint8_t*data){
 guards(0);assert(!vx_image_commit(s,r,id,image_crc32(data,width*height*4)));memcpy((uint8_t*)im_memory+64,data,width*height*4);guards(0);
}
static vx_image_plan render(vx_image_state*s,image_params params){
 vx_image_plan p;guards(0);assert(!vx_image_prepare(s,&params,&p));
 for(unsigned i=0;i<p.count;i++){
  const image_job*j=&p.jobs[i];unsigned count=width*height;
  if(p.ops[i]==IMAGE_PIXEL_MEAN)count/=j->param0*j->param0;
  assert(!vx_image_job_validate(p.ops[i],j));
  assert(!im_run(img_kernels[p.ops[i]],img_kernel_words[p.ops[i]],j,count));
  guards(0);assert(!vx_image_accept(s,&p,i,0));jobs++;
 }
 uint32_t slot,offset;assert(!vx_image_slot(p.output_token,width*height*4,0,&slot,&offset));
 uint32_t crc=image_crc32((uint8_t*)im_memory+slot*IMAGE_SLOT_SIZE+offset,width*height*4);
 assert(!vx_image_finish(s,&p,crc));hits+=p.cache_hit;guards(0);return p;
}
static void save(const char*dir,const char*name,uint32_t token){
 char path[1024];int length=snprintf(path,sizeof(path),"%s/%s",dir,name);assert(length>0&&(size_t)length<sizeof(path));
 uint32_t slot,off;assert(!vx_image_slot(token,width*height*4,0,&slot,&off));FILE*f=fopen(path,"wb");assert(f);
 assert(fwrite((uint8_t*)im_memory+slot*IMAGE_SLOT_SIZE+off,1,width*height*4,f)==width*height*4);assert(!fclose(f));
}
int main(int argc,char**argv){
 assert(argc==6);const char*mode=argv[1],*dir=argv[4];int full=!strcmp(mode,"full");
 width=full?1280:256;height=full?720:144;im_memory=calloc(VX_IMAGE_RESERVED_BYTES/4,4);assert(im_memory);
 uint8_t *base=malloc(width*height*4),*source=malloc(width*height*4);assert(base&&source);read_exact(argv[2],base,width*height*4);
 guards(1);read_exact(argv[5],(uint8_t*)im_memory+(IMAGE_TABLE-IMAGE_SLOT_BASE),65536);
 vx_image_state s;assert(!vx_image_init(&s,width,height));image_rect rect=full?(image_rect){0,0,width,height}:(image_rect){3,4,247,131};
 if(!strcmp(mode,"replay")){
  FILE*f=fopen(argv[3],"r");assert(f);unsigned tx,id,style,preset,block,active=0,count=0;
  while(fscanf(f,"%u %u %u %u %u",&tx,&id,&style,&preset,&block)==5){
   if(id!=active){
    memcpy(source,base,width*height*4);
    for(unsigned i=0;i<width*height*4;i+=4){unsigned x=(i/4)%width,y=i/(4*width);if(x>=3&&x<250&&y>=4&&y<135)source[i]^=(uint8_t)(id*17);else{source[i]=7;source[i+1]=16;source[i+2]=30;}}
    commit(&s,&rect,id,source);active=id;
   }
   vx_image_plan p=render(&s,(image_params){style,preset,block});char name[64];snprintf(name,sizeof(name),"tx-%u.rgba",tx);save(dir,name,p.output_token);count++;
  }
  fclose(f);assert(count==51&&jobs==87&&hits==10);
 }else if(full){
  commit(&s,&rect,1,base);
  for(unsigned style=1;style<=4;style++){
   image_params params={style,(style==1||style==4)?1:0,style==4?4:0};vx_image_plan p=render(&s,params);char name[64];snprintf(name,sizeof(name),"style-%u.rgba",style);save(dir,name,p.output_token);
   unsigned before=jobs;vx_image_plan cached=render(&s,params);assert(cached.cache_hit&&!cached.count&&before==jobs);
  }
 }else{
  assert(!strcmp(mode,"colors"));commit(&s,&rect,1,base);const unsigned styles[]={3,2,1,4};
  for(unsigned k=0;k<4;k++)for(unsigned preset=0;preset<(styles[k]==2?1:7);preset++){
   image_params params={styles[k],preset,styles[k]==4?8:0};vx_image_plan p=render(&s,params);char name[64];snprintf(name,sizeof(name),"style-%u-p%u.rgba",styles[k],preset);save(dir,name,p.output_token);
   unsigned before=jobs;vx_image_plan cached=render(&s,params);assert(cached.cache_hit&&!cached.count&&before==jobs);
  }
  for(unsigned block=2;block<=4;block+=2)for(unsigned preset=0;preset<7;preset++){
   vx_image_plan p=render(&s,(image_params){4,preset,block});char name[64];snprintf(name,sizeof(name),"style-4-p%u-b%u.rgba",preset,block);save(dir,name,p.output_token);
  }
 }
 guards(0);printf("U14_IMAGE_ORIGINAL_RV32_WORDS_NEW_RECIPE_PASS_HOST mode=%s jobs=%u hits=%u gpu_raw_cycles=null\n",mode,jobs,hits);free(base);free(source);free(im_memory);return 0;
}

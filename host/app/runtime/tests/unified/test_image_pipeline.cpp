#include "image/pipeline_linux.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
static void check(bool b,const char*s){if(!b){std::fprintf(stderr,"U14_IMAGE_ASSERT %s\n",s);std::exit(83);}}
static uint32_t marker(unsigned id,unsigned style,unsigned preset,unsigned block){return id*10000+style*1000+preset*10+block;}
int main(int argc,char**argv){
 check(argc==2,"frozen51 requests path");
 check(IMAGE_SLOT_COUNT==33&&uint64_t(IMAGE_SLOT_COUNT)*IMAGE_SLOT_SIZE==VX_IMAGE_RESERVED_BYTES,"33 logical slots132MiB");
 vx_image_state s{};check(!vx_image_init(&s,256,144),"diagnostic dimensions");
 image_rect rect{3,4,247,131};FILE*f=std::fopen(argv[1],"r");check(f,"requests");
 unsigned tx,id,style,preset,block,count=0,jobs=0,hits=0,active=0;
 while(std::fscanf(f,"%u %u %u %u %u",&tx,&id,&style,&preset,&block)==5){
  if(id!=active){check(!vx_image_commit(&s,&rect,id,id*17),"immutable input epoch");active=id;}
  image_params params{style,preset,block};vx_image_plan p{};
  check(!vx_image_prepare(&s,&params,&p),"bounded image recipe must prepare");
  check(p.width==256&&p.height==144&&p.input_id==id&&p.count<=11,"bound geometry/pass count");
  check(style||p.count==0,"original zero GPU jobs");jobs+=p.count;hits+=p.cache_hit;count++;
  vx_image_plan duplicate{};check(vx_image_prepare(&s,&params,&duplicate)==-EBUSY,"one pending immutable plan");
  for(unsigned i=0;i<p.count;i++){
   check(!vx_image_job_validate(p.ops[i],&p.jobs[i]),"all packet token/table/flags predicates");
   if(style==2)check(p.jobs[i].flags==IMAGE_GRAY_Q8,"Q8 follows all XDoG passes");
   check(!vx_image_accept(&s,&p,i,0),"ordered once ACK metadata");
  }
  check(!vx_image_finish(&s,&p,style?marker(id,style,preset,block):id*17),"complete transaction caches result");
  check(!s.busy&&!s.first_error,"metadata completion");
 }
 std::fclose(f);check(count==51&&jobs==87&&hits==10,"frozen51 replay exactly87jobs10hits");
 unsigned slot=99,offset=99;check(!vx_image_slot(IMAGE_THEME+32,32,0,&slot,&offset)&&slot==10&&offset==64+0x1e00+32,"checked table subview");
 check(vx_image_slot(IMAGE_TABLE,65536,1,&slot,&offset)==-EACCES,"GPU table never writable");
 check(vx_image_slot(IMAGE_ACTIVE-1,4,0,&slot,&offset)!=0&&vx_image_slot(0xffffffff,4,0,&slot,&offset)!=0,"no guard/overflow token resolution");
 vx_image_state bad{};check(vx_image_init(&bad,512,512)==-EINVAL,"only original two geometries");
 check(!vx_image_init(&bad,1280,720),"native720 dimensions");image_rect full{0,0,1280,720};check(!vx_image_commit(&bad,&full,1,1),"720 commit");
 image_params p0{4,0,4};vx_image_plan p{};check(!vx_image_prepare(&bad,&p0,&p)&&p.count==2,"cold mean and Bayer");
 image_job changed=p.jobs[1];changed.src1=IMAGE_ACTIVE;check(vx_image_job_validate(p.ops[1],&changed)!=0,"Bayer source identity");
 check(vx_image_accept(&bad,&p,0,-EIO)==-EIO&&bad.first_error==-EIO,"first job failure sticky");
 check(vx_image_prepare(&bad,&p0,&p)==-EIO&&!bad.results[15].valid,"failed chain cannot create cache");
 puts("U14_IMAGE_RECIPE_33SLOTS51_REQUESTS87JOBS10HITS_PASS_HOST_METADATA gpu_raw_cycles=null");
}

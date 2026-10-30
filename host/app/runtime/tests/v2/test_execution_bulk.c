/* HOST-only: execute the real private validation paths against checked IO.
 * This is not a GPU, CDMA timing or board performance measurement. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <vxl/v2/execution.h>
#include <vxl/hunet.h>
#include "../../driver/v2/execution.c"
struct bulk_fixture {
 struct vxl_v2_ctx ctx;struct vxl_v2_hw hw;struct vxl_v2_execution execution;
 struct vxl_v2_program program;struct vxl_v2_job job;
 vxl_u8 data[3][12288];vxl_u64 clock;unsigned reads,max_read,fail_read;
};
static vxl_u64 test_now(void *p){return ++((struct bulk_fixture *)p)->clock;}
static int checked_copy(void *p,struct vxl_v2_object *o,vxl_u64 off,void *b,vxl_u64 n,int write){
 struct bulk_fixture *f=p;assert(!write && n && n<=12288 && off+n<=o->length);
 f->reads++;if(n>f->max_read)f->max_read=(unsigned)n;
 if(f->fail_read==f->reads)return -EIO;
 memcpy(b,f->data[o-f->ctx.objects]+off,(size_t)n);return 0;
}
static const struct vxl_v2_memory_ops checked_ops={0,0,checked_copy};
static struct vxl_v2_ref reference(unsigned slot,vxl_u64 length){
 struct vxl_v2_ref r={slot+1,1,0,length,1,VXL_PL,1};return r;
}
static struct bulk_fixture *fixture(void){
 struct bulk_fixture *f=calloc(1,sizeof(*f));assert(f);
 f->ctx.owner=f->ctx.epoch=1;f->ctx.ops=&checked_ops;f->ctx.memory_context=f;
 f->hw.ctx=&f->ctx;f->hw.os=f;f->hw.now_ns=test_now;f->execution.hw=&f->hw;
 f->execution.program=&f->program;f->execution.job=&f->job;
 for(unsigned i=0;i<3;i++){
  struct vxl_v2_object *o=&f->ctx.objects[i];o->handle=i+1;o->generation=o->owner=o->epoch=1;
  o->domain=VXL_PL;o->access=1;o->length=sizeof(f->data[i]);
  for(unsigned k=0;k<sizeof(f->data[i]);k++)f->data[i][k]=(vxl_u8)(k*17+i);
 }
 f->program.owned[0]=reference(0,64);vxl_hunet_hash(f->data[0],64,f->program.entry.code);
 return f;
}
static void hash_blocks(void){
 struct bulk_fixture *f=fixture();struct vxl_v2_ref r=reference(1,8193);vxl_u8 got[32],want[32];
 vxl_hunet_hash(f->data[1],8193,want);assert(hash(&f->execution,&r,got)==0 && !memcmp(got,want,32));
 printf("hash reads=%u max=%u expected=3/4096\n",f->reads,f->max_read);fflush(stdout);
 assert(f->reads==3 && f->max_read==4096);
 f->reads=0;f->fail_read=2;memset(got,0x5c,32);assert(hash(&f->execution,&r,got)==-EIO && f->reads==2);
 for(unsigned i=0;i<32;i++)assert(got[i]==0x5c);
 /* Existing ref resolver delegates object bounds to vxl_range_check, whose
  * established out-of-capacity result is EINVAL (not execution's ERANGE). */
 f->fail_read=0;r.offset=12280;r.length=16;assert(hash(&f->execution,&r,got)==-EINVAL);
 r.offset=~(vxl_u64)0;r.length=2;assert(hash(&f->execution,&r,got)==-EOVERFLOW);
 free(f);
}
static void exact_aliases(void){
 struct bulk_fixture *f=fixture();struct vxl_v2_execution *x=&f->execution;
 f->job.num_refs=2;f->job.refs[0]=f->job.refs[1]=reference(1,8193);
 assert(sources(x,1)==0);
 printf("source reads=%u expected=4 for code+two exact aliases\n",f->reads);fflush(stdout);
 assert(f->reads==4 && !memcmp(x->input_sha[0],x->input_sha[1],32));
 f->reads=0;assert(sources(x,0)==0 && f->reads==4);
 x->input_sha[1][0]^=1;assert(sources(x,0)==-EBADMSG);x->input_sha[1][0]^=1;
 f->data[1][8192]^=1;assert(sources(x,0)==-EBADMSG);f->data[1][8192]^=1;
 f->job.refs[1].offset=64;f->job.refs[1].length=8129;f->reads=0;
 assert(sources(x,1)==0 && f->reads==6); /* code + 3 + 2 */
 f->job.refs[1]=f->job.refs[0];f->job.refs[1].object_generation=2;assert(sources(x,1)==-ESTALE);
 f->job.refs[1]=f->job.refs[0];f->job.refs[1].device_generation=2;assert(sources(x,1)==-ESTALE);
 f->job.refs[1]=f->job.refs[0];f->job.refs[1].domain=VXL_PS;assert(sources(x,1)==-EINVAL);
 f->job.refs[1]=f->job.refs[0];f->ctx.objects[1].owner=2;assert(sources(x,1)==-EPERM);
 free(f);
}
static void indirect_blocks(void){
 struct bulk_fixture *f=fixture();struct vxl_v2_execution *x=&f->execution;vxl_u32 faces=7;
 vxl_u32 *pairs=(vxl_u32 *)f->data[1],*indices=(vxl_u32 *)f->data[2];
 x->policy.directory_ref=0;x->policy.indices_ref=1;x->policy.tile_count=513;
 x->policy.max_faces=7;x->policy.max_indices=1026;memcpy(x->shadow,&faces,4);
 f->job.num_refs=2;f->job.refs[0]=reference(1,513*8);f->job.refs[1]=reference(2,1026*4);
 for(unsigned i=0;i<513;i++){pairs[2*i]=2*i;pairs[2*i+1]=2;}
 for(unsigned i=0;i<1026;i++)indices[i]=i%faces;
 assert(indirect(x)==0);
 printf("indirect reads=%u max=%u expected=4/4096\n",f->reads,f->max_read);fflush(stdout);
 assert(f->reads==4 && f->max_read==4096);
 f->reads=0;f->fail_read=2;assert(indirect(x)==-EIO);f->fail_read=0;
 f->reads=0;f->fail_read=4;assert(indirect(x)==-EIO);f->fail_read=0;
 pairs[1024]--;assert(indirect(x)==-ERANGE);pairs[1024]++;
 indices[1025]=faces;assert(indirect(x)==-ERANGE);indices[1025]=0;
 pairs[1025]=~0u;assert(indirect(x)==-ERANGE);pairs[1025]=2;
 f->job.refs[1].length--;assert(indirect(x)==-ERANGE);f->job.refs[1].length++;
 f->job.refs[0].length--;assert(indirect(x)==-ERANGE);f->job.refs[0].length++;
 f->job.refs[0].access=3;assert(indirect(x)==-EACCES);
 free(f);
}
int main(int argc,char **argv){
 assert(argc==2);
 if(!strcmp(argv[1],"hash"))hash_blocks();
 else if(!strcmp(argv[1],"aliases"))exact_aliases();
 else if(!strcmp(argv[1],"indirect"))indirect_blocks();
 else return 2;
 puts("V2_BULK_VALIDATION_PASS_HOST_NOT_BOARD");return 0;
}

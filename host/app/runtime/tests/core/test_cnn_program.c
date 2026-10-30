#include <vxl/engine.h>
#include <vxl/cnn.h>
#include <assert.h>
#include <stdio.h>
#include "../../generated/cnn/programs.h"
static const vxl_u32 *programs[]={vxl_cnn_program_2,vxl_cnn_program_3,vxl_cnn_program_4,vxl_cnn_program_5};
static const unsigned counts[9]={64,18,2,72,72,18,36,2,2};
static const unsigned index_by_id[4][4]={{0,1,2,3},{3,99,99,4},{4,99,99,5},{5,6,7,8}};
static void init(struct vxl_engine *e,vxl_u32 w[16],unsigned id) {
 unsigned i,j;memset(e,0,sizeof(*e));memset(w,0,64);
 for(i=0;i<9;i++){
  struct vxl_object *o=&e->objects[i];o->handle=i+1;o->gpu=0x80000000+i*0x1000;
  o->bus=VXL_PL_BASE+i*0x1000;o->domain=VXL_PL;o->purpose=VXL_DATA;
  o->length=o->allocated=128+((counts[i]*4+63)&~63U);
 }
 w[0]=VXL_CNN_MAGIC;w[1]=64;w[2]=1;w[3]=id;
 for(j=0;j<4;j++){i=index_by_id[id-2][j];if(i!=99){w[4+j]=e->objects[i].gpu+64;w[8+j]=counts[i];}}
}
int main(void) {
 unsigned id,i,j;vxl_u8 bad[4096];struct vxl_engine e;vxl_u32 w[16],copy[16];
 for(id=2;id<=5;id++){
  const vxl_u8 *p=(const vxl_u8 *)programs[id-2];
  assert(!vxl_program_check_id(p,4096,VXL_CNN_CODE_GPU,id));
  assert(vxl_program_check_id(p,4096,VXL_CNN_CODE_GPU,1)==-EACCES);
  assert(vxl_program_check_id(p,4096,VXL_CNN_CODE_GPU,99)==-EACCES);
  assert(vxl_program_check_id(p,4096,VXL_CNN_CODE_GPU,id==5?2:id+1)==-EACCES);
  assert(vxl_program_check_id(p,4095,VXL_CNN_CODE_GPU,id)==-EACCES);
  assert(vxl_program_check_id(p,4096,VXL_CNN_CODE_GPU+4,id)==-EACCES);
  for(i=0;i<4096;i++){memcpy(bad,p,4096);bad[i]^=1;assert(vxl_program_check_id(bad,4096,VXL_CNN_CODE_GPU,id)==-EACCES);}
  init(&e,w,id);assert(!vxl_cnn_args_check(&e,id,w));
  for(i=0;i<16;i++)if(i<4||i>=8){memcpy(copy,w,64);copy[i]^=1;assert(vxl_cnn_args_check(&e,id,copy)==-EINVAL);}
  for(j=0;j<4;j++){
   unsigned obj=index_by_id[id-2][j];if(obj==99){memcpy(copy,w,64);copy[4+j]=0x80000040;assert(vxl_cnn_args_check(&e,id,copy)==-EINVAL);continue;}
   memcpy(copy,w,64);copy[4+j]+=4;assert(vxl_cnn_args_check(&e,id,copy)==-EINVAL);
   memcpy(copy,w,64);copy[4+j]+=64;assert(vxl_cnn_args_check(&e,id,copy)==-ERANGE);
   e.objects[obj].length-=64;assert(vxl_cnn_args_check(&e,id,w)==-ERANGE);init(&e,w,id);
   e.objects[obj].domain=VXL_PS;assert(vxl_cnn_args_check(&e,id,w)==-ERANGE);init(&e,w,id);
   e.objects[obj].purpose=VXL_CODE;assert(vxl_cnn_args_check(&e,id,w)==-ERANGE);init(&e,w,id);
   e.objects[obj].handle=0;assert(vxl_cnn_args_check(&e,id,w)==-ERANGE);init(&e,w,id);
   memcpy(copy,w,64);copy[4+j]=0xffffffc0;assert(vxl_cnn_args_check(&e,id,copy)==-ERANGE);
  }
  memcpy(copy,w,64);copy[7]=copy[4];assert(vxl_cnn_args_check(&e,id,copy)==-ERANGE);
 }
 memset(bad,0,4096);assert(vxl_program_check_id(bad,4096,VXL_CNN_CODE_GPU,1)==-EACCES);
 assert(vxl_cnn_args_check(&e,6,w)==-EINVAL);
 puts("CNN_PROGRAM_ARGS_REJECTION_PASS_SOFTWARE_ONLY");return 0;
}

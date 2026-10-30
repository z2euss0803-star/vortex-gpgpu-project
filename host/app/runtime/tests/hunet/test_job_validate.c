#include <vxl/hunet.h>
#include <assert.h>
#include <stdio.h>
int main(int argc,char **argv){
 FILE *f;struct vxl_hunet_ctx c={0};struct vxl_hunet_job_v1 j,bad;unsigned i,w,counts[14]={0};vxl_u32 words[32];
 assert(argc==2);f=fopen(argv[1],"rb");assert(f);c.program_id=6;
 for(i=1;i<=1023;i++){
  assert(fread(&j,1,128,f)==128);assert(!memcmp(&j,vxl_hunet_catalog_job(i),128));
  {int rc=vxl_hunet_validate_job(&c,&j,i);if(rc)fprintf(stderr,"sequence=%u op=%u rc=%d\n",i,j.op,rc);assert(!rc);}counts[j.op]++;
  for(w=0;w<32;w++){memcpy(words,&j,128);words[w]^=1;memcpy(&bad,words,128);assert(vxl_hunet_validate_job(&c,&bad,i)!=0);}
  assert(vxl_hunet_validate_job(&c,&j,i+1)!=0);
 }
 assert(fgetc(f)==EOF);fclose(f);
 for(i=1;i<=13;i++){unsigned expected[13]={314,314,314,16,16,13,8,12,2,1,1,8,4};assert(counts[i]==expected[i-1]);}
 memcpy(&j,vxl_hunet_catalog_job(1),128);j.y-=64;assert(vxl_hunet_validate_spans(&j,vxl_hunet_catalog_matrix(0))!=0);
 {struct vxl_hunet_matrix_v1 m=*vxl_hunet_catalog_matrix(0);m.kp=0xffffffffu;assert(vxl_hunet_validate_spans(&j,&m)!=0);}
 printf("HUNET_1023_EXACT_TEMPLATES_32736_WORD_MUTATIONS_REJECTED_NOT_GPU\n");return 0;
}

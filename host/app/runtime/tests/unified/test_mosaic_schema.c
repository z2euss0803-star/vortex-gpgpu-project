#include <vxl/v2/program.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x,why) do{if(!(x)){fprintf(stderr,"U13_MOSAIC_SCHEMA_ASSERT %s\n",why);return 78;}}while(0)
int main(int argc,char**argv){
 unsigned char bytes[65536],changed[65536];struct vxl_v2_schema_header h;struct vxl_v2_schema_field f;size_t n;unsigned int i,addresses=0,zero=0;
 const unsigned int access[8]={1,2,3,1,1,3,3,3},alignment[8]={4,4,2,2,4,4,4,64};FILE*file;
 CHECK(argc==2,"schema file argument");file=fopen(argv[1],"rb");CHECK(file,"schema open");n=fread(bytes,1,sizeof(bytes),file);CHECK(!ferror(file)&&feof(file),"complete bounded schema file");CHECK(!fclose(file),"schema close");
 CHECK(n>=64&&!vxl_v2_schema_check(bytes,(vxl_u32)n,128),"actual native C schema checker");memcpy(&h,bytes,64);
 CHECK(h.code_gpu==0x80000000&&h.rodata_gpu==0x80110000&&h.crt_gpu==0x10002000&&h.crt_bytes==4096&&h.stack_gpu==0xfff00000&&h.stack_bytes==0x100000&&h.provider_flags==1,"Mosaic S8 code RO CRT stack identity");
 CHECK(h.field_count==23&&n==1536,"complete exact new schema, not prior library");
 for(i=0;i<h.field_count;i++){
  memcpy(&f,bytes+64+i*64,64);
  if(f.kind==VXL_V2_ADDRESS32){unsigned int role=f.data_ref;CHECK(role<8&&f.arg_offset==16+role*4&&f.access==access[role]&&f.alignment==alignment[role]&&f.element_bytes==1&&f.count_offsets[0]==48+role*4&&f.count_offsets[1]==0xffffffffu&&f.count_offsets[2]==0xffffffffu&&f.relative_arg_offset==0xffffffffu&&!f.constant_bytes,"eight exact typed relocated role rules");CHECK(!(addresses&(1u<<role)),"one address rule per data ref");addresses|=1u<<role;}
  if(f.kind==VXL_V2_ZERO_RANGE){CHECK(f.arg_offset==124&&f.constant_bytes==4,"private reserved word zero");zero++;}
  if(f.kind==VXL_V2_SCALAR32&&f.arg_offset==116)CHECK(f.min_value==1&&f.max_value==4096,"original bounded batch limit");
 }
 CHECK(addresses==255&&zero==1,"all refs and reserved field covered");
 memcpy(changed,bytes,n);changed[0]^=1;CHECK(vxl_v2_schema_check(changed,(vxl_u32)n,128)<0,"bad magic");
 memcpy(changed,bytes,n);changed[28]^=1;CHECK(vxl_v2_schema_check(changed,(vxl_u32)n,128)<0,"bad CRT alignment");
 memcpy(changed,bytes,n);memcpy(&f,changed+64,64);f.arg_offset=4;memcpy(changed+64,&f,64);CHECK(vxl_v2_schema_check(changed,(vxl_u32)n,128)<0,"duplicate scalar leaves uncovered bytes");
 for(i=0;i<h.field_count;i++){
  memcpy(&f,bytes+64+i*64,64);if(f.kind!=VXL_V2_ADDRESS32)continue;
  memcpy(changed,bytes,n);f.data_ref=8;memcpy(changed+64+i*64,&f,64);CHECK(vxl_v2_schema_check(changed,(vxl_u32)n,128)<0,"ninth data ref forbidden");
  memcpy(&f,bytes+64+i*64,64);memcpy(changed,bytes,n);f.count_offsets[0]=16;memcpy(changed+64+i*64,&f,64);CHECK(vxl_v2_schema_check(changed,(vxl_u32)n,128)<0,"extent must read checked scalar not address");
 }
 puts("U13_MOSAIC_SEALED_SCHEMA_NATIVE_C_RULES_AND_MUTATIONS_PASS_HOST gpu_raw_cycles=null");return 0;
}

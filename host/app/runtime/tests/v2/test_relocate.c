#include "program_fixture.h"
int main(int argc,char **argv){
 unsigned char raw[2048],code[64]={0};size_t n;struct vxl_v2_catalog cat;struct vxl_v2_catalog_entry e;struct vxl_v2_ctx c;struct vxl_v2_program_table table={0};struct vxl_v2_program_ref ref;struct program_mem mem={0};struct vxl_v2_provider provider=test_provider();struct vxl_v2_ref data[7],args;unsigned i;vxl_u32 input[8]={0,0,0,0,0,0,0,16},out[8]={0};struct vxl_v2_job j={.major=2,.size=512,.cookie=1,.device_generation=1,.execution_budget_ns=100,.args_bytes=32,.num_refs=7};
 assert(argc==2);n=read_fixture(argv[1],raw,sizeof(raw));assert(vxl_v2_catalog_open(&cat,raw,(vxl_u32)n,&provider)==0);assert(vxl_v2_catalog_lookup(&cat,raw+320,&e)==0);
 vxl_v2_init(&c,&program_ops,&mem);assert(vxl_v2_table_bind(&c,&table,&cat)==0);assert(vxl_v2_owner_enter(&c,1,1)==0);assert(vxl_v2_program_load(&c,1,&e,code,0,&ref)==0);
 for(i=0;i<7;i++){assert(vxl_v2_buffer_create(&c,1,1,2,3,3,64,&data[i])==0);j.refs[i]=data[i];}
 assert(vxl_v2_buffer_create(&c,1,1,2,2,3,32,&args)==0);assert(vxl_v2_buffer_copy(&c,1,&args,input,1)==0);
 j.args_handle=args.handle;j.args_generation=args.object_generation;j.program_handle=ref.handle;j.program_generation=ref.generation;
 assert(vxl_v2_build_args(&c,&table.programs[0],&j,out,sizeof(out))==0);
 for(i=0;i<7;i++)assert(out[i]==0x80000000u+64*i);
 assert(out[7]==16 && j.num_refs==7 && c.live==10);
 input[0]=0x12345678;assert(vxl_v2_buffer_copy(&c,1,&args,input,1)==0);assert(vxl_v2_build_args(&c,&table.programs[0],&j,out,sizeof(out))==-EINVAL);input[0]=0;
 input[7]=65;assert(vxl_v2_buffer_copy(&c,1,&args,input,1)==0);assert(vxl_v2_build_args(&c,&table.programs[0],&j,out,sizeof(out))==-ERANGE);input[7]=16;
 assert(vxl_v2_buffer_copy(&c,1,&args,input,1)==0);j.refs[6].length=8;assert(vxl_v2_build_args(&c,&table.programs[0],&j,out,sizeof(out))==-ERANGE);
 assert(vxl_v2_program_release(&c,1,&ref)==0);assert(vxl_v2_owner_close(&c,1,1)==0 && !mem.live);
 puts("V2_SEVEN_DATA_REFS_PLUS_ARGS_CODE_NO_TRUNCATION_RELOCATION_PASS_NOT_GPU");return 0;
}

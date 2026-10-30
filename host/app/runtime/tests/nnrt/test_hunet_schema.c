#include "tests/v2/program_fixture.h"
int main(int argc,char**argv){
 unsigned char raw[4096],code[64]={0};size_t n;struct vxl_v2_catalog cat;struct vxl_v2_catalog_entry e;struct vxl_v2_ctx c;struct vxl_v2_program_table table={0};struct vxl_v2_program_ref program;struct program_mem mem={0};struct vxl_v2_provider provider=test_provider();struct vxl_v2_ref prefix,gap,input,output,workspace,args;unsigned i;
 vxl_u32 packet[32]={1,128,9,0},out[32];struct vxl_v2_job j={.major=2,.size=512,.cookie=1,.device_generation=1,.execution_budget_ns=100,.args_bytes=128,.num_refs=8};
 assert(argc==2);n=read_fixture(argv[1],raw,sizeof(raw));assert(vxl_v2_catalog_open(&cat,raw,(vxl_u32)n,&provider)==0);assert(vxl_v2_catalog_lookup(&cat,raw+320,&e)==0);assert(e.logical_program==0x10010);
 vxl_v2_init(&c,&program_ops,&mem);assert(vxl_v2_table_bind(&c,&table,&cat)==0);assert(vxl_v2_owner_enter(&c,1,1)==0);assert(vxl_v2_program_load(&c,1,&e,code,0,&program)==0);
 /* First-fit fills small holes: one large prefix alone does NOT protect the
  * gap before fixed ARGS. Fill BOTH actual fixture holes before high data.
  * This is a protocol fixture, not the production arena proof (U20). */
 assert(vxl_v2_buffer_create(&c,1,1,2,3,3,0x100000,&gap)==0);
 assert(vxl_v2_buffer_create(&c,1,1,2,3,3,0x6fff40,&prefix)==0);
 assert(vxl_v2_buffer_create(&c,1,1,2,3,3,16,&input)==0);assert(vxl_v2_buffer_create(&c,1,1,2,3,3,4,&output)==0);assert(vxl_v2_buffer_create(&c,1,1,2,3,3,16,&workspace)==0);
 for(i=0;i<6;i++){j.refs[i]=input;j.refs[i].access=1;packet[12+i]=16;}j.refs[6]=output;j.refs[6].access=2;packet[18]=4;j.refs[7]=workspace;packet[19]=16;
 packet[20]=packet[21]=packet[22]=packet[24]=packet[25]=1;packet[23]=4; /* GAP FP32 [1,1,4] -> [1,1,1]. */
 assert(vxl_v2_buffer_create(&c,1,1,2,2,3,128,&args)==0);assert(vxl_v2_buffer_copy(&c,1,&args,packet,1)==0);j.args_handle=args.handle;j.args_generation=args.object_generation;j.program_handle=program.handle;j.program_generation=program.generation;
 assert(vxl_v2_build_args(&c,&table.programs[0],&j,out,sizeof(out))==0);
 for(i=0;i<6;i++)assert(out[4+i]>=0x80800000u&&out[4+i]==out[4]);assert(out[10]!=out[4]&&out[11]!=out[4]&&out[11]!=out[10]);assert(!memcmp(out,packet,16)&&!memcmp(out+12,packet+12,80));
 packet[4]=0x80800000;assert(vxl_v2_buffer_copy(&c,1,&args,packet,1)==0);assert(vxl_v2_build_args(&c,&table.programs[0],&j,out,sizeof(out))==-EINVAL);packet[4]=0;
 packet[12]=67108865;assert(vxl_v2_buffer_copy(&c,1,&args,packet,1)==0);assert(vxl_v2_build_args(&c,&table.programs[0],&j,out,sizeof(out))==-ERANGE);packet[12]=16;assert(vxl_v2_buffer_copy(&c,1,&args,packet,1)==0);
 j.refs[7].length=8;assert(vxl_v2_build_args(&c,&table.programs[0],&j,out,sizeof(out))==-ERANGE);j.refs[7]=workspace;j.refs[7].access=1;assert(vxl_v2_build_args(&c,&table.programs[0],&j,out,sizeof(out))==-EACCES);j.refs[7]=workspace;j.refs[7].object_generation++;assert(vxl_v2_build_args(&c,&table.programs[0],&j,out,sizeof(out))==-ESTALE);
 assert(vxl_v2_program_release(&c,1,&program)==0);assert(vxl_v2_owner_close(&c,1,1)==0&&!mem.live);
 puts("U12_HUNET_EIGHT_REF_SEALED_SCHEMA_REAL_V2_RELOCATION_PASS_HOST_NOT_GPU");return 0;
}

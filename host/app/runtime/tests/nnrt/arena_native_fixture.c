#include "tests/v2/program_fixture.h"
/* Execute the actual frozen allocator, program loader and relocation source.
 * All storage is malloc; no register I/O, GPU start, DMA or device access. */
int arena_native_fixture(const char*catalog_path,const char*code_path,int prefix,vxl_u32 out[32]){
 unsigned char raw[4096],code[65537],guard[64],got[64];size_t n;struct vxl_v2_catalog cat;struct vxl_v2_catalog_entry e;struct vxl_v2_ctx c;struct vxl_v2_program_table table={0};struct vxl_v2_program_ref program;struct program_mem mem={0};struct vxl_v2_provider provider=test_provider();struct vxl_v2_ref arena,args,view;unsigned i,baseline;vxl_u64 skip=prefix?0x800000:0;
 vxl_u32 packet[32]={1,128,9,0};struct vxl_v2_job j={.major=2,.size=512,.cookie=1,.device_generation=1,.execution_budget_ns=100,.args_bytes=128,.num_refs=8};
 n=read_fixture(catalog_path,raw,sizeof(raw));assert(vxl_v2_catalog_open(&cat,raw,(vxl_u32)n,&provider)==0);assert(vxl_v2_catalog_lookup(&cat,raw+320,&e)==0);n=read_fixture(code_path,code,sizeof(code));assert(n==65536&&e.code_bytes==n);
 vxl_v2_init(&c,&program_ops,&mem);assert(vxl_v2_table_bind(&c,&table,&cat)==0);assert(vxl_v2_owner_enter(&c,1,1)==0);assert(vxl_v2_program_load(&c,1,&e,code,0,&program)==0);baseline=mem.live;assert(baseline==4);
 /* One public DATA root contains the internal unused prefix. No gap/prefix
  * fill objects and no fixed-address user allocator or address query. */
 assert(vxl_v2_buffer_create(&c,1,1,VXL_PL,VXL_V2_DATA,3,skip+512,&arena)==0);assert(mem.live==baseline+1);
 for(i=0;i<6;i++){j.refs[i]=arena;j.refs[i].offset=skip+64;j.refs[i].length=16;j.refs[i].access=1;packet[12+i]=16;}
 j.refs[6]=arena;j.refs[6].offset=skip+192;j.refs[6].length=4;j.refs[6].access=2;packet[18]=4;j.refs[7]=arena;j.refs[7].offset=skip+320;j.refs[7].length=16;j.refs[7].access=3;packet[19]=16;
 packet[20]=packet[21]=packet[22]=packet[24]=packet[25]=1;packet[23]=4;
 assert(vxl_v2_buffer_create(&c,1,1,VXL_PL,VXL_V2_ARGS,3,128,&args)==0);assert(vxl_v2_buffer_copy(&c,1,&args,packet,1)==0);j.args_handle=args.handle;j.args_generation=args.object_generation;j.program_handle=program.handle;j.program_generation=program.generation;
 assert(vxl_v2_build_args(&c,&table.programs[0],&j,out,128)==0);for(i=0;i<6;i++){assert(out[4+i]==out[4]);}assert(out[10]-out[4]==128&&out[11]-out[4]==256);if(prefix){assert(out[4]>=0x80800000u&&out[11]+16<=0xfff00000u);}else{assert(out[4]<0x80800000u);}
 memset(guard,0xa7,sizeof(guard));view=arena;view.offset=skip+128;view.length=64;assert(vxl_v2_buffer_copy(&c,1,&view,guard,1)==0);
 for(i=0;i<8;i++){assert(vxl_v2_buffer_pin(&c,1,&j.refs[i],2)==0);}assert(vxl_v2_buffer_release(&c,1,&arena)==-EBUSY);assert(vxl_v2_buffer_copy(&c,1,&view,got,0)==-EBUSY);
 for(i=0;i<4;i++){assert(vxl_v2_buffer_unpin(&c,1,&j.refs[i],2)==0);}assert(vxl_v2_buffer_copy(&c,1,&view,got,0)==-EBUSY);
 for(i=4;i<8;i++){assert(vxl_v2_buffer_unpin(&c,1,&j.refs[i],2)==0);}assert(vxl_v2_buffer_copy(&c,1,&view,got,0)==0&&!memcmp(guard,got,64));
 j.refs[7].offset=skip+504;assert(vxl_v2_build_args(&c,&table.programs[0],&j,packet,128)==-EINVAL);assert(vxl_v2_buffer_release(&c,1,&arena)==0);assert(vxl_v2_buffer_release(&c,1,&args)==0);assert(mem.live==baseline);assert(vxl_v2_program_release(&c,1,&program)==0);assert(vxl_v2_owner_close(&c,1,1)==0&&!mem.live);return 0;
}

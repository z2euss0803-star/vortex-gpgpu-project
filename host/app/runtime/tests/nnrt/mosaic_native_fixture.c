#include "tests/v2/program_fixture.h"
int mosaic_native_fixture(const char*catalog,const char*code,const char*ro,const vxl_u32*args,vxl_u32 used,vxl_u32*out){
 unsigned char raw[4096],code_bytes[65537],*ro_bytes=malloc(6307841);size_t n;unsigned i,baseline;
 struct vxl_v2_catalog cat;struct vxl_v2_catalog_entry e;struct vxl_v2_ctx c;struct vxl_v2_program_table table={0};struct vxl_v2_program_ref program,cached;
 struct program_mem mem={0};struct vxl_v2_provider provider=test_provider(),wrong=provider;struct vxl_v2_ref arena,packet,view;
 struct vxl_v2_job j={.major=2,.size=512,.cookie=1,.device_generation=1,.execution_budget_ns=100,.args_bytes=128,.num_refs=8};
 vxl_u32 changed[32],scratch[32];vxl_u64 offsets[8],total=0x800000,work;assert(ro_bytes&&args&&out&&(used&128));
 n=read_fixture(catalog,raw,sizeof(raw));assert(!vxl_v2_catalog_open(&cat,raw,(vxl_u32)n,&provider));wrong.flags=2;assert(vxl_v2_catalog_open(&cat,raw,(vxl_u32)n,&wrong)<0);assert(!vxl_v2_catalog_open(&cat,raw,(vxl_u32)n,&provider));assert(!vxl_v2_catalog_lookup(&cat,raw+320,&e));
 assert(e.namespace_id==1&&e.logical_profile==1&&e.logical_program==65568&&e.code_bytes==65536&&e.rodata_bytes==6307840);
 assert(read_fixture(code,code_bytes,sizeof(code_bytes))==65536&&read_fixture(ro,ro_bytes,6307841)==6307840);
 vxl_v2_init(&c,&program_ops,&mem);assert(!vxl_v2_table_bind(&c,&table,&cat));assert(!vxl_v2_owner_enter(&c,1,1));
 code_bytes[0]^=1;assert(vxl_v2_program_load(&c,1,&e,code_bytes,ro_bytes,&program)==-EBADMSG&&!mem.live);code_bytes[0]^=1;
 ro_bytes[6307839]^=1;assert(vxl_v2_program_load(&c,1,&e,code_bytes,ro_bytes,&program)==-EBADMSG&&!mem.live);ro_bytes[6307839]^=1;
 assert(!vxl_v2_program_load(&c,1,&e,code_bytes,ro_bytes,&program));baseline=mem.live;assert(baseline==5);
 assert(!vxl_v2_program_load(&c,1,&e,code_bytes,ro_bytes,&cached)&&cached.handle==program.handle&&mem.live==baseline);assert(!vxl_v2_program_release(&c,1,&cached));
 /* No user fixed-address allocator: a single DATA root owns its views.
  * Actual checked kernel allocator skips resident CODE/RO/ARGS reservations. */
 for(i=0;i<8;i++)if(used&(1u<<i)){offsets[i]=total;total+=(args[12+i]+63ULL)&~63ULL;}
 work=offsets[7];
#ifdef VX_MOSAIC_LEGACY_V1
 for(i=0;i<8;i++)if(!(used&(1u<<i)))offsets[i]=work;
#else
 /* Separately budget64 bytes for the current v2 unused readonly roles. */
 {vxl_u64 readonly=total;total+=64;for(i=0;i<8;i++)if(!(used&(1u<<i)))offsets[i]=(i==0||i==3||i==4)?readonly:work;}
#endif
 assert(!vxl_v2_buffer_create(&c,1,1,VXL_PL,VXL_V2_DATA,3,total,&arena));assert(!vxl_v2_buffer_create(&c,1,1,VXL_PL,VXL_V2_ARGS,3,128,&packet));
 assert(!vxl_v2_buffer_copy(&c,1,&packet,(void*)args,1));j.args_handle=packet.handle;j.args_generation=packet.object_generation;j.program_handle=program.handle;j.program_generation=program.generation;
 for(i=0;i<8;i++){j.refs[i]=arena;j.refs[i].offset=offsets[i];j.refs[i].length=args[12+i];j.refs[i].access=(i==0||i==3||i==4)?1:i==1?2:3;}
#ifndef VX_MOSAIC_LEGACY_V1
 for(i=0;i<8;i++)if(j.refs[i].access==1)for(unsigned k=0;k<8;k++)if(j.refs[k].access&2)assert(!(offsets[i]<offsets[k]+j.refs[k].length&&offsets[k]<offsets[i]+j.refs[i].length));
#endif
 assert(!vxl_v2_build_args(&c,&table.programs[0],&j,out,128));
 for(i=0;i<8;i++){
  vxl_u64 length=j.refs[i].length;j.refs[i].length--;assert(vxl_v2_build_args(&c,&table.programs[0],&j,scratch,128)<0);j.refs[i].length=length;
  j.refs[i].object_generation++;assert(vxl_v2_build_args(&c,&table.programs[0],&j,scratch,128)==-ESTALE);j.refs[i].object_generation--;
  memcpy(changed,args,128);changed[4+i]=0x80800000;assert(!vxl_v2_buffer_copy(&c,1,&packet,changed,1));assert(vxl_v2_build_args(&c,&table.programs[0],&j,scratch,128)<0);
 }
 memcpy(changed,args,128);changed[31]=1;assert(!vxl_v2_buffer_copy(&c,1,&packet,changed,1));assert(vxl_v2_build_args(&c,&table.programs[0],&j,scratch,128)==-EINVAL);
 memcpy(changed,args,128);changed[29]=4097;assert(!vxl_v2_buffer_copy(&c,1,&packet,changed,1));assert(vxl_v2_build_args(&c,&table.programs[0],&j,scratch,128)==-ERANGE);
 assert(!vxl_v2_buffer_copy(&c,1,&packet,(void*)args,1));
 for(i=0;i<8;i++)assert(!vxl_v2_buffer_pin(&c,1,&j.refs[i],2));
 assert(vxl_v2_buffer_release(&c,1,&arena)==-EBUSY);view=arena;view.offset=work;view.length=64;assert(vxl_v2_buffer_copy(&c,1,&view,scratch,0)==-EBUSY);
 for(i=0;i<7;i++){assert(!vxl_v2_buffer_unpin(&c,1,&j.refs[i],2));}assert(vxl_v2_buffer_copy(&c,1,&view,scratch,0)==-EBUSY);
 assert(!vxl_v2_buffer_unpin(&c,1,&j.refs[7],2));assert(!vxl_v2_buffer_copy(&c,1,&view,scratch,0));
 assert(!vxl_v2_buffer_release(&c,1,&arena)&&!vxl_v2_buffer_release(&c,1,&packet)&&mem.live==baseline);assert(!vxl_v2_program_release(&c,1,&program));assert(!vxl_v2_owner_close(&c,1,1)&&!mem.live);free(ro_bytes);return 0;
}

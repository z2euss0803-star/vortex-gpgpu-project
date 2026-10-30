#include "program_fixture.h"
int main(int argc,char **argv){
 unsigned char raw[2048],code[64]={0};size_t n;struct vxl_v2_catalog cat;struct vxl_v2_catalog_entry e,forged;struct vxl_v2_ctx c;struct vxl_v2_program_table table={0};struct vxl_v2_program_ref ref={0},again;struct program_mem mem={0};struct vxl_v2_provider provider=test_provider();
 assert(argc==2);n=read_fixture(argv[1],raw,sizeof(raw));assert(vxl_v2_catalog_open(&cat,raw,(vxl_u32)n,&provider)==0);assert(vxl_v2_catalog_lookup(&cat,raw+320,&e)==0);
 vxl_v2_init(&c,&program_ops,&mem);assert(vxl_v2_table_bind(&c,&table,&cat)==0);assert(vxl_v2_owner_enter(&c,1,1)==0);
 code[0]=1;assert(vxl_v2_program_load(&c,1,&e,code,0,&ref)==-EBADMSG);assert(c.live==0 && mem.live==0);code[0]=0;
 forged=e;forged.logical_program++;assert(vxl_v2_program_load(&c,1,&forged,code,0,&ref)==-EKEYREJECTED);
 assert(vxl_v2_program_load(&c,1,&e,code,0,&ref)==0);assert(ref.handle && ref.device_generation==1 && c.live==2 && mem.live==2);
 assert(vxl_v2_program_load(&c,1,&e,code,0,&again)==0);assert(again.handle==ref.handle && c.live==2);
 {struct vxl_v2_ref alias=table.programs[0].owned[0];alias.access=3;assert(vxl_v2_buffer_copy(&c,1,&alias,code,1)==-EACCES);assert(vxl_v2_buffer_release(&c,1,&alias)==-EBUSY);}
 table.programs[0].pins=1;assert(vxl_v2_program_release(&c,1,&ref)==-EBUSY);table.programs[0].pins=0;
 assert(vxl_v2_program_release(&c,1,&again)==0 && c.live==2);assert(vxl_v2_program_release(&c,1,&ref)==0 && c.live==0 && mem.live==0);
 assert(vxl_v2_program_release(&c,1,&ref)==-EBADF);
 raw[n-1]^=1;assert(vxl_v2_program_load(&c,1,&e,code,0,&ref)==-EBADMSG);assert(c.live==0);raw[n-1]^=1;
 assert(vxl_v2_program_load(&c,1,&e,code,0,&ref)==0);
 ((unsigned char *)c.objects[0].storage)[0]^=1;
 assert(vxl_v2_program_load(&c,1,&e,code,0,&again)==-EBADMSG);assert(c.quarantined && c.live==2 && mem.live==2);
 /* Only destroy injected host storage after asserting retention. No driver reset. */
 {unsigned i;for(i=0;i<64;i++)if(c.objects[i].handle)free(c.objects[i].storage);}
 puts("V2_CONTROLLED_LOAD_SHA_SEAL_REFCOUNT_UNLOAD_PASS_NOT_GPU");return 0;
}

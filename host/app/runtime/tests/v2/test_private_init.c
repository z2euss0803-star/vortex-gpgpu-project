#include "program_fixture.h"
int main(int argc,char **argv){
 unsigned char raw[2048],code[64]={0};size_t n;struct vxl_v2_catalog cat;struct vxl_v2_catalog_entry e;struct vxl_v2_ctx c;struct vxl_v2_program_table table={0};struct vxl_v2_program_ref ref;struct program_mem mem={0};struct vxl_v2_provider p=test_provider();unsigned i,k;
 assert(argc==2);n=read_fixture(argv[1],raw,sizeof(raw));assert(vxl_v2_catalog_open(&cat,raw,(vxl_u32)n,&p)==0);assert(vxl_v2_catalog_lookup(&cat,raw+320,&e)==0);
 vxl_v2_init(&c,&program_ops,&mem);assert(vxl_v2_table_bind(&c,&table,&cat)==0);assert(vxl_v2_owner_enter(&c,1,1)==0);assert(vxl_v2_program_load(&c,1,&e,code,0,&ref)==0);
 assert(c.live==4);for(i=0;i<64;i++)if(c.objects[i].handle){const unsigned char *b=c.objects[i].storage;for(k=0;k<c.objects[i].length;k++)assert(b[k]==0);}
 assert(vxl_v2_program_release(&c,1,&ref)==0 && !mem.live);puts("V2_ARGS_CRT_STACK_EXPLICIT_ZERO_FROM_DIRTY_STORAGE_PASS_NOT_GPU");return 0;
}

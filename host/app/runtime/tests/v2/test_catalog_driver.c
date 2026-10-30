#include "program_fixture.h"
struct files {unsigned char raw[2048];size_t n;vxl_u32 mode,uid;unsigned reads,releases;};
static int read_owned(void *p,const char *path,vxl_u32 max,struct vxl_v2_file_blob *b){
 struct files *f=p;const char *prefix="/lib/firmware/vxl/programs/";f->reads++;
 b->uid=f->uid;b->mode=f->mode;b->regular=1;b->no_follow=1;
 if(!strcmp(path,"/lib/firmware/vxl/catalog-v1.bin")){b->length=(vxl_u32)f->n;b->bytes=malloc(f->n);assert(b->bytes && max>=f->n);memcpy(b->bytes,f->raw,f->n);}
 else{assert(!strncmp(path,prefix,strlen(prefix)) && strlen(path)==strlen(prefix)+68 && !strcmp(path+strlen(path)-4,".bin"));b->length=64;b->bytes=calloc(1,64);assert(b->bytes && max>=64);}
 return 0;
}
static void release_owned(void *p,struct vxl_v2_file_blob *b){struct files *f=p;f->releases++;free(b->bytes);memset(b,0,sizeof(*b));}
int main(int argc,char **argv){
 struct files f={.mode=0444};struct vxl_v2_file_ops io={&f,read_owned,release_owned};struct vxl_v2_provider p=test_provider();struct vxl_v2_file_blob blob={0};struct vxl_v2_catalog cat;struct vxl_v2_ctx c;struct vxl_v2_program_table t={0};struct vxl_v2_program_ref r;struct program_mem mem={0};unsigned releases;
 assert(argc==2);f.n=read_fixture(argv[1],f.raw,sizeof(f.raw));f.mode=0666;
 assert(vxl_v2_read_catalog_file(&io,&p,&cat,&blob)==-EACCES);assert(f.releases==1 && !blob.bytes);
 f.mode=0444;f.uid=1000;assert(vxl_v2_read_catalog_file(&io,&p,&cat,&blob)==-EACCES);f.uid=0;
 assert(vxl_v2_read_catalog_file(&io,&p,&cat,&blob)==0 && blob.bytes);
 vxl_v2_init(&c,&program_ops,&mem);assert(vxl_v2_table_bind(&c,&t,&cat)==0);assert(vxl_v2_owner_enter(&c,1,1)==0);
 f.mode=0666;assert(vxl_v2_load_entry_file(&c,1,blob.bytes+320,&io,&r)==-EACCES);assert(c.live==0);f.mode=0444;releases=f.releases;
 assert(vxl_v2_load_entry_file(&c,1,blob.bytes+320,&io,&r)==0 && f.releases==releases+1);assert(vxl_v2_program_release(&c,1,&r)==0);
 assert(vxl_v2_owner_close(&c,1,1)==0);release_owned(&f,&blob);
 puts("V2_FIXED_TRUSTED_PATH_OWNER_MODE_AND_PRIVATE_LOAD_PASS_HOST_FILE_BOUNDARY");return 0;
}

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <vxl/hunet.h>
#include <vxl/engine.h>
int main(int argc,char **argv){
 const struct vxl_opt_catalog *cat=vxl_opt_catalog_default();struct vxl_opt_release r={0};struct vxl_engine e={0};struct vxl_hunet_ctx c={0};struct vxl_hunet_release old={0};struct vxl_opt_meta m;vxl_u32 words[32];
 e.profile_id=4;c.engine=&e;c.release=&old;r.catalog=cat;c.opt.release=&r;c.program_id=10;
 if(argc==4&&!strcmp(argv[1],"--graph")){vxl_u8 descriptor[4096];FILE*f=fopen(argv[2],"rb");assert(f);assert(fread(words,1,128,f)==128);assert(fgetc(f)==EOF);fclose(f);f=fopen(argv[3],"rb");assert(f);size_t n=fread(descriptor,1,sizeof(descriptor),f);assert(fgetc(f)==EOF);fclose(f);return vxl_opt_graph_validate(words,128,descriptor,n)?1:0;}
 if(argc==4&&!strcmp(argv[1],"--job")){FILE*f=fopen(argv[2],"rb");assert(f);assert(fread(words,1,128,f)==128);assert(fgetc(f)==EOF);fclose(f);return vxl_opt_validate_job(&c,words,(vxl_u32)strtoul(argv[3],NULL,10),&m)?1:0;}
 for(unsigned i=0;i<cat->gpu_jobs;i++){
  assert(!vxl_opt_validate_job(&c,cat->jobs[i],cat->metadata[i].first,&m));assert(!memcmp(&m,&cat->metadata[i],sizeof(m)));
  for(unsigned j=0;j<32;j++){memcpy(words,cat->jobs[i],128);words[j]^=1;assert(vxl_opt_validate_job(&c,words,cat->metadata[i].first,&m));}
  if(cat->metadata[i].block_id){
   vxl_u32 descriptor[1024];vxl_u32 bytes=cat->jobs[i][10];const vxl_u8 *expected=cat->graph_data+(cat->metadata[i].block_id-1)*4096;
   assert(!vxl_opt_descriptor_matches(cat,cat->jobs[i],expected,bytes));
   for(unsigned j=0;j<bytes/4;j++){memcpy(descriptor,expected,bytes);descriptor[j]^=1;assert(vxl_opt_descriptor_matches(cat,cat->jobs[i],descriptor,bytes));}
  }
 }
 memcpy(words,cat->jobs[0],128);assert(vxl_opt_validate_job(&c,words,2,&m));c.program_id=9;assert(vxl_opt_validate_job(&c,words,1,&m));c.program_id=10;
 c.sealed=1;assert(vxl_hunet_check_access(&c,0x84000000,4096,1)==-EACCES);
 c.sealed=0;
 struct vxl_hunet_binding_v1*b=&r.binding.v3.v2.v1;memset(&r.binding,1,sizeof(r.binding));b->magic=VXL_HUNET_BINDING_MAGIC;b->version=4;b->bytes=512;b->profile_id=4;b->program_id=10;b->features=31;b->generation=0;b->state=0;
 memset(r.binding.v3.v2.reserved,0,32);memset(r.binding.v3.reserved,0,32);memcpy(b->jobs_sha256,cat->jobs_sha,32);memcpy(r.binding.v3.descriptors_sha256,cat->descriptors_sha,32);memcpy(r.binding.v3.layout_sha256,cat->layout_sha,32);r.wire_bytes=8192;r.ro_bytes=4096;r.digest_backend=(cat->runtime_flags&1u)?VXL_DIGEST_AUTO:VXL_DIGEST_PORTABLE;
 memcpy(r.wire_sha,b->program_sha256,32);old.binding=*b;vxl_hunet_hash(&r.binding,sizeof(r.binding),r.binding_sha);assert(!vxl_opt_init(&c,&r));r.binding_sha[0]^=1;assert(vxl_opt_init(&c,&r)==-ESTALE);
 puts("EXACT_CATALOG_ALL_JOBS_ALL_32_WORD_MUTATIONS_SEAL_ACCESS_BINDING_PASS_NOT_GPU");return 0;
}

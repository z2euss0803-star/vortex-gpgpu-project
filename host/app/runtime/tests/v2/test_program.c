#include <vxl/v2/program.h>
#include <vxl/hunet.h>
#include <assert.h>
#include <stdio.h>
static void payload_sha(unsigned char *raw,size_t n){vxl_hunet_hash(raw+64,n-64,raw+16);}
/* Golden wire generated independently from LE layout by fixture script. Not GPU code. */
int main(int argc,char **argv){
 unsigned char raw[2048];FILE *f;size_t n;struct vxl_v2_catalog c;struct vxl_v2_catalog_entry e;struct vxl_v2_provider p={.flags=1};
 assert(argc==2);f=fopen(argv[1],"rb");assert(f);n=fread(raw,1,sizeof(raw),f);assert(feof(f));fclose(f);
 memset(p.hardware_contract,1,32);memset(p.bit,2,32);memset(p.xsa,3,32);
 assert(vxl_v2_catalog_open(&c,raw,(vxl_u32)n,&p)==0);assert(c.count==1);
 assert(vxl_v2_catalog_lookup(&c,raw+64+64+192,&e)==0);assert(e.namespace_id==1 && e.logical_program==0x10001);
 raw[n-1]^=1;assert(vxl_v2_catalog_open(&c,raw,(vxl_u32)n,&p)==-EBADMSG);raw[n-1]^=1;
 p.hardware_contract[0]^=1;assert(vxl_v2_catalog_open(&c,raw,(vxl_u32)n,&p)==-EXDEV);p.hardware_contract[0]^=1;
 {unsigned char saved[2048];memcpy(saved,raw,n);
  ((struct vxl_v2_catalog_header *)(void *)raw)->count=17;assert(vxl_v2_catalog_open(&c,raw,(vxl_u32)n,&p)==-EINVAL);memcpy(raw,saved,n);
  ((struct vxl_v2_catalog_entry *)(void *)(raw+64))->flags=1;payload_sha(raw,n);assert(vxl_v2_catalog_open(&c,raw,(vxl_u32)n,&p)==-EINVAL);memcpy(raw,saved,n);
  ((struct vxl_v2_catalog_entry *)(void *)(raw+64))->code_offset=0xffffffffu;payload_sha(raw,n);assert(vxl_v2_catalog_open(&c,raw,(vxl_u32)n,&p)==-EINVAL);memcpy(raw,saved,n);
  ((struct vxl_v2_catalog_entry *)(void *)(raw+64))->schema_offset=128;payload_sha(raw,n);assert(vxl_v2_catalog_open(&c,raw,(vxl_u32)n,&p)==-EINVAL);memcpy(raw,saved,n);
  ((struct vxl_v2_schema_header *)(void *)(raw+576))->field_count=0;payload_sha(raw,n);assert(vxl_v2_catalog_open(&c,raw,(vxl_u32)n,&p)==-EINVAL);memcpy(raw,saved,n);
  assert(vxl_v2_catalog_open(&c,raw,(vxl_u32)n,&p)==0);raw[n-1]^=1;assert(vxl_v2_catalog_lookup(&c,saved+320,&e)==-EBADMSG);memcpy(raw,saved,n);
 }
 puts("V2_CATALOG_TRUST_BOUNDS_AND_SCHEMA_PASS_NOT_BOARD");return 0;
}

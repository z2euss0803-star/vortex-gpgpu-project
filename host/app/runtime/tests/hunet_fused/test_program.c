#include <vxl/engine.h>
#include <vxl/hunet.h>
#include <assert.h>
#include <stdio.h>
#include "../../generated/hunet_fused/descriptors.h"
#include "../../generated/hunet_fused/layout.h"
struct memory{vxl_u8 code[16],desc[20096];unsigned writes;};
static int read_bytes(void*p,vxl_u64 a,void*b,vxl_u64 n,int w){
 struct memory*m=p;vxl_u8*source=NULL;
 if(w){m->writes++;return -EACCES;}
 if(a>=VXL_HUNET_CODE&&a+n<=VXL_HUNET_CODE+16)source=m->code+(a-VXL_HUNET_CODE);
 if(a>=VXL_FUSED_DESCRIPTORS_START&&a+n<=VXL_FUSED_DESCRIPTORS_START+20096)source=m->desc+(a-VXL_FUSED_DESCRIPTORS_START);
 if(!source)return -ERANGE;
 memcpy(b,source,n);return 0;
}
int main(void){
 struct vxl_hunet_ctx c={0};struct vxl_engine e={0};struct vxl_fused_release r={0};struct memory m={{0},{0},0};unsigned i;
 for(i=0;i<16;i++)m.code[i]=(vxl_u8)i;
 memcpy(m.desc,vxl_fused_descriptors,20096);r.wire_bytes=16;r.ro_bytes=8;
 vxl_hunet_hash(m.code,16,r.wire_sha);vxl_hunet_hash(m.code,8,r.ro_sha);
 vxl_hunet_hash(m.desc,20096,r.binding.descriptors_sha256);
 c.engine=&e;c.program_id=9;c.fused.release=&r;c.io.context=&m;c.io.rw=read_bytes;e.owner=123;e.generation=4;
 assert(!vxl_fused_check_program(&c));assert(c.seal_owner==123&&c.seal_generation==4);
 m.code[12]^=1;assert(vxl_fused_check_program(&c)==-EACCES);m.code[12]^=1;
 c.program_sealed=1;m.code[12]^=1;assert(!vxl_fused_check_program(&c));m.code[0]^=1;
 assert(vxl_fused_check_program(&c)==-EACCES);m.code[0]^=1;
 m.desc[64]^=1;assert(vxl_fused_check_program(&c)==-EACCES);m.desc[64]^=1;
 r.binding.descriptors_sha256[0]^=1;assert(vxl_fused_check_program(&c)==-ESTALE);r.binding.descriptors_sha256[0]^=1;
 e.generation++;assert(vxl_fused_check_program(&c)==-ESTALE);
 assert(!m.writes);puts("FUSED_REAL_HASH_READONLY_DESCRIPTOR_AND_GENERATION_REJECTION_PASS_NOT_GPU");
}

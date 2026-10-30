#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <vxl/hunet.h>
#include <vxl/engine.h>
struct transport {vxl_u8 *matrix;vxl_u32 size;unsigned reads,writes;int fail_read,fail_write,corrupt;};
static int rw(void *p,vxl_u64 address,void *buffer,vxl_u64 bytes,int write){
 struct transport *t=p;assert(bytes<=4096&&!(address&15)&&!(bytes&15));if(address<0x86d0df80||vxl_range_check(t->size,address-0x86d0df80,bytes))return -ERANGE;
 if(write){t->writes++;if(t->fail_write)return -EIO;memcpy(t->matrix+address-0x86d0df80,buffer,bytes);if(t->corrupt)t->matrix[address-0x86d0df80]^=1;}
 else {t->reads++;if(t->fail_read)return -EIO;memcpy(buffer,t->matrix+address-0x86d0df80,bytes);}return 0;
}
int main(void){
 struct vxl_opt_release r={.catalog=vxl_opt_catalog_default()};struct transport t={0};struct vxl_hunet_ctx c={0};struct vxl_engine e={0};c.engine=&e;t.size=r.catalog->matrix_bytes;t.matrix=calloc(1,t.size);assert(t.matrix);c.program_id=10;c.opt.release=&r;c.io.context=&t;c.io.rw=rw;
 assert(!vxl_opt_restore_descriptors(&c));assert(t.reads&&t.writes&&memcmp(t.matrix,r.catalog->matrix_data,t.size)==0);
 t.corrupt=1;assert(vxl_opt_restore_descriptors(&c)==-EIO);t.corrupt=0;t.fail_read=1;assert(vxl_opt_restore_descriptors(&c)==-EIO);t.fail_read=0;t.fail_write=1;assert(vxl_opt_restore_descriptors(&c)==-EIO);
 free(t.matrix);puts("ACTUAL_DESCRIPTOR_RESTORE_READBACK_CORRUPTION_AND_IO_FAILURE_PASS_NOT_DMA_NOT_GPU");
}

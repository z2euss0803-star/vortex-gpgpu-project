#include <vxl/engine.h>
#include <vxl/hunet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
struct memory { unsigned char code[8388608],ws[VXL_HUNET_WS_BYTES]; };
static int rw(void *p,vxl_u64 a,void *b,vxl_u64 n,int w) {
 struct memory *m=p;
 if(a>=VXL_HUNET_WS&&!vxl_range_check(sizeof(m->ws),a-VXL_HUNET_WS,n)){if(w)memcpy(m->ws+a-VXL_HUNET_WS,b,n);else memcpy(b,m->ws+a-VXL_HUNET_WS,n);return 0;}
 if(a<0x80000000ULL||vxl_range_check(sizeof(m->code),a-0x80000000ULL,n))return -ERANGE;
 if(w)memcpy(m->code+a-0x80000000ULL,b,n);else memcpy(b,m->code+a-0x80000000ULL,n);return 0;
}
int main(int argc,char **argv) {
 struct vxl_engine e;struct vxl_hunet_ctx c;struct vxl_hunet_release r;
 struct memory *m=calloc(1,sizeof(*m));struct vxl_hunet_io io={m,rw,0};FILE *f;unsigned char b[4097],digest[32];
 assert(argc==2&&m);vxl_engine_init_profile(&e,0,0,1,4);e.owner=123;e.generation=2;
 vxl_hunet_release_defaults(&r);assert(!vxl_hunet_layout_init(&c,&e,&r));c.io=io;
 assert(vxl_pool_reserve(&e.pools[0],0x2000,64)==-EBUSY);
 assert(vxl_pool_reserve(&e.pools[0],0x10040,128)==-EBUSY);
 assert(vxl_pool_reserve(&e.pools[0],0xff000,4096)==-EBUSY);
 assert(vxl_pool_reserve(&e.pools[1],0x7ff00000,64)==-EBUSY);
 assert(vxl_hunet_check_access(&c,0xfffffff0ULL,16,0)==-EACCES);
 assert(vxl_hunet_check_access(&c,~(vxl_u64)0-4,8,0)==-EOVERFLOW);
 assert(vxl_hunet_check_access(&c,0x10002000,64,1)==-EACCES);
 f=fopen(argv[1],"rb");assert(f);assert(fread(m->code,1,r.program[0].wire_bytes,f)==r.program[0].wire_bytes);fclose(f);
 assert(!vxl_hunet_check_program(&c,1));
 m->code[r.program[0].ro_bytes+32]^=1;assert(!vxl_hunet_check_program(&c,0));
 m->code[100]^=1;assert(vxl_hunet_check_program(&c,0)==-EACCES);
 assert(vxl_hunet_check_program(&c,1)==-EACCES);m->code[100]^=1;
 memset(b,0x6b,sizeof(b));memset(m->code+0x100000,0xa5,8192);
 assert(!vxl_hunet_io_bytes(&c,0x80100001,b,sizeof(b),1));
 assert(m->code[0x100000]==0xa5&&m->code[0x101002]==0xa5);
 assert(!vxl_hunet_io_bytes(&c,0x8010000f,b,sizeof(b),1));
 assert(m->code[0x10000e]==0x6b&&m->code[0x101010]==0xa5);
 vxl_hunet_hash("abc",3,digest);assert(digest[0]==0xba&&digest[31]==0xad);
 assert(!vxl_hunet_check_access(&c,0x80000000,64,1));c.sealed=1;
 assert(vxl_hunet_check_access(&c,0x80000000,64,1)==-EACCES);
 assert(!vxl_hunet_guards(&c,1));assert(!vxl_hunet_guards(&c,0));m->ws[0]^=1;assert(vxl_hunet_guards(&c,0)==-EIO);
 puts("HUNET_RESERVED_RANGES_RMW_SECOND_SUBMIT_RO_TAMPER_PASS_NOT_GPU");free(m);return 0;
}

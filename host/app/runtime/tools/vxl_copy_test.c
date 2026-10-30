#include <vxl/transport.h>
#include <stdio.h>
#include <string.h>
static void init(struct vxl_request *r){memset(r,0,sizeof(*r));r->abi_major=1;r->struct_size=sizeof(*r);}
int main(int argc,char **argv){struct vxl_transport t;struct vxl_request r;unsigned char src[4097],dst[4097];vxl_u64 h=0;unsigned i;int mock=argc==2&&!strcmp(argv[1],"--mock"),rc=vxl_transport_open(&t,mock);
 if(rc)return 1;
 init(&r);r.domain=VXL_PL;r.purpose=VXL_DATA;r.length=8192;r.alignment=64;rc=vxl_transport_call(&t,VXL_ALLOC,&r,sizeof(r));if(rc)goto done;h=r.handle;
 for(i=0;i<sizeof(src);i++)src[i]=(unsigned char)(i*29+5);
 init(&r);r.handle=h;r.offset=63;r.length=sizeof(src);r.user_ptr=(uintptr_t)src;rc=vxl_transport_call(&t,VXL_COPY_TO,&r,sizeof(r));if(rc)goto done;
 r.user_ptr=(uintptr_t)dst;rc=vxl_transport_call(&t,VXL_COPY_FROM,&r,sizeof(r));if(rc)goto done;if(memcmp(src,dst,sizeof(src)))rc=-EIO;
done:
 if(h){init(&r);r.handle=h;if(vxl_transport_call(&t,VXL_FREE,&r,sizeof(r))&&!rc)rc=-EIO;}
 printf("{\"scope\":\"%s\",\"copy_bytes\":4097,\"offset\":63,\"result\":%d}\n",mock?"CPU_MOCK":"REAL_DRIVER",rc);
 vxl_transport_close(&t);return rc?1:0;
}

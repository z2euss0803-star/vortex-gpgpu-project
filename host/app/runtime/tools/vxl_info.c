#include <vxl/transport.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv){struct vxl_transport t;struct vxl_request r={0};int mock=argc==2&&!strcmp(argv[1],"--mock"),rc=vxl_transport_open(&t,mock);
 if(rc){fprintf(stderr,"open: %d\n",rc);return 1;}r.abi_major=1;r.struct_size=sizeof(r);rc=vxl_transport_call(&t,VXL_GET_INFO,&r,sizeof(r));
 if(!rc)printf("{\"scope\":\"%s\",\"state\":%u,\"generation\":%u,\"capacity\":%llu,\"used\":%llu}\n",mock?"CPU_MOCK":"REAL_DRIVER",r.state,r.generation,(unsigned long long)r.capacity,(unsigned long long)r.used);
 vxl_transport_close(&t);return rc?1:0;
}

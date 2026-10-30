#include <vxl/transport.h>
#include <assert.h>
#include <stdio.h>
static unsigned calls;
int __wrap_close(int fd){assert(fd==123);calls++;errno=EINTR;return -1;}
int main(void){struct vxl_transport t={123,0};assert(vxl_transport_close_checked(&t)==-EINTR);assert(t.fd==-1&&calls==1);assert(!vxl_transport_close_checked(&t)&&calls==1);assert(vxl_transport_close_checked(0)==-EINVAL);puts("CLOSE_EINTR_FD_INVALIDATED_NOT_RETRIED_PASS");}

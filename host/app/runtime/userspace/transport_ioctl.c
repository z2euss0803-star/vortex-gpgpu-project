#include <vxl/transport.h>
#include <vxl/mock.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <time.h>
int vxl_transport_open(struct vxl_transport *t,int mock) {
 int rc;memset(t,0,sizeof(*t));t->fd=-1;
 if(mock){struct vxl_mock *m=calloc(1,sizeof(*m));if(!m)return -ENOMEM;vxl_mock_init(m);rc=vxl_engine_open(&m->engine,1);if(rc){free(m);return rc;}t->mock=m;return 0;}
 t->fd=open("/dev/vortex0",O_RDWR|O_CLOEXEC);return t->fd<0?-errno:0;
}
int vxl_transport_call(struct vxl_transport *t,vxl_u32 cmd,void *p,vxl_u32 size) {
 struct vxl_request *r=p;int rc=vxl_uapi_validate(cmd,r,size,0);vxl_u64 spent=0,n;
 if(rc)return rc;
 if(t->mock){struct vxl_mock *m=t->mock;
  do {rc=vxl_engine_call(&m->engine,1,cmd,r);if(cmd!=VXL_WAIT||rc!=-ETIMEDOUT||spent>=r->wait_ms)break;
   n=r->wait_ms-spent;if(n>5)n=5;vxl_mock_advance(m,n);spent+=n;
  }while(1);return rc;
 }
 if(t->fd<0)return -EBADF;
 return ioctl(t->fd,cmd,r)<0?-errno:0;
}
void vxl_transport_close(struct vxl_transport *t) {
 (void)vxl_transport_close_checked(t);
}
int vxl_transport_close_checked(struct vxl_transport *t) {
 int fd,rc=0;if(!t)return -EINVAL;
 if(t->mock){struct vxl_mock *m=t->mock;vxl_engine_close(&m->engine,1);
  /* Synthetic in-flight objects remain resident exactly as the lifecycle test requires. */
  if(!m->engine.job.pinned)free(m);
  t->mock=0;
 }else if(t->fd>=0){fd=t->fd;t->fd=-1;if(close(fd))rc=-errno;}
 t->fd=-1;return rc;
}

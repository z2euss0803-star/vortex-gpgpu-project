#include <vxl/demo_runner.h>
#include <errno.h>
int demo_owner_acquire(struct demo_session *s){
 int fd;
 if(!s||s->quarantined)return -EBUSY;
 if(s->owner_fd>=0)return 0;
 if(!s->gpu.open_owner)return -ENOSYS;
 fd=s->gpu.open_owner(s->gpu.context);
 if(fd<0)return fd;
 s->owner_fd=fd;return 0;
}
int demo_owner_release(struct demo_session *s){
 int rc;
 if(!s)return -EINVAL;
 if(s->owner_fd<0)return 0;
 if(!s->gpu.quiescent||s->gpu.quiescent(s->gpu.context,s->owner_fd)!=1)return -EBUSY;
 if(!s->gpu.close_owner)return -ENOSYS;
 rc=s->gpu.close_owner(s->gpu.context,s->owner_fd);
 if(rc)return rc;
 s->owner_fd=-1;return 0;
}

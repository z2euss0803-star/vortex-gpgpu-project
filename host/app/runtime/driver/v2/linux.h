#ifndef VXL_V2_LINUX_H
#define VXL_V2_LINUX_H
#include <vxl/v2/dispatch.h>
#include <linux/wait.h>
#include <linux/poll.h>
struct vxl_device;
struct vxl_v2_linux {
 struct vxl_v2_port port;struct vxl_device *device;wait_queue_head_t waitq;
 int (*snapshot)(struct vxl_v2_linux *,vxl_u64,struct vxl_v2_snapshot *);
};
/* Only the matching U06 startup provider may install this, before misc_register.
 * Zero/NULL keeps the entire legacy open/ioctl/worker path unchanged. */
int vxl_v2_linux_bind(struct vxl_device *,struct vxl_v2_linux *,struct vxl_v2_ctx *,
 int (*snapshot)(struct vxl_v2_linux *,vxl_u64,struct vxl_v2_snapshot *));
int vxl_v2_linux_startup(struct vxl_device *);
__poll_t vxl_v2_linux_poll(struct file *,poll_table *);
#endif

#ifndef VXL_INTERNAL_H
#define VXL_INTERNAL_H
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/dma-mapping.h>
#include <linux/gpio/consumer.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/ktime.h>
#include <vxl/engine.h>
#include <vxl/hunet.h>
#include <vxl/submit_timing.h>
#include <vxl/mgmt_timing.h>
#include <vxl/s8.h>
#include <vxl/s8_trace.h>
#include <vxl/mosaic_runtime.h>
#include <vxl/profile_switch.h>
struct vxl_device;
struct vxl_v2_linux;
struct vxl_s8_file_key {struct vxl_device *device;unsigned file;};
struct vxl_device {
 struct vxl_mgmt_trace mgmt_trace;
 struct vxl_submit_trace submit_trace;
 bool cdma_fastwait_active; /* scoped SUBMIT or approved CPU/ROI management call; independent of trace capacity */
 struct device *dev;
 struct miscdevice misc;
 struct mutex mutex;
 struct delayed_work worker;
 struct vxl_v2_linux *compute; /* startup-only capability-isolated v2 provider */
 struct vxl_engine engine;
 struct vxl_profile_factory profile_factory;
 struct vxl_device *profile_candidate;
 struct mosaic_ctx mosaic;
 struct mosaic_host_state mosaic_state;
 struct vxl_s8_ctx s8;
 struct vxl_s8_trace s8_trace;
 struct vxl_s8_file_key s8_files[3];
 struct vxl_hunet_ctx hunet;
 struct vxl_hunet_release release;
 struct vxl_hybrid_release hybrid_release;
 struct vxl_fused_release fused_release;
 struct vxl_opt_release opt_release;
 void __iomem *gpu,*cdma;
 void *ps;
 dma_addr_t ps_dma;
 struct gpio_desc *ready;
 struct clk_bulk_data *clks;
 int nclks;
 struct reset_control *reset;
 u32 cdma_last;
 bool approved;
};
int vxl_mgmt_trace_init(struct vxl_device *d);
const struct vxl_digest_ops *vxl_kernel_crypto_ops(void);
extern const struct file_operations vxl_fops;
extern const struct vxl_ops vxl_hw_ops;
int vxl_hunet_attach(struct vxl_device *);
int vxl_hunet_candidate(struct vxl_device *,struct vxl_device *,void *);
int vxl_s8_candidate(struct vxl_device *,struct vxl_engine *,struct vxl_s8_ctx *);
int vxl_profile_setup(struct vxl_device *);
int vxl_profile_select(struct vxl_device *,vxl_u64,struct vxl_request *);
int vxl_private_rw(void *,vxl_u64,void *,vxl_u64,int);
int vxl_hybrid_hw_idle(struct vxl_hunet_ctx *);
int vxl_submit_trace_init(struct vxl_device *);
int vxl_s8_attach(struct vxl_device *);
int vxl_s8_hw_snapshot(struct vxl_device *,struct vxl_event *);
int vxl_s8_tracefs_init(struct vxl_device *);
int vxl_mosaic_attach(struct vxl_device *);
int vxl_mosaic_candidate(struct vxl_device *,struct vxl_engine *,struct mosaic_ctx *,struct mosaic_host_state *);
#endif

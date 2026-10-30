/* Startup-only provider installation. There is no profile-switch ioctl. */
#include "../vxl_internal.h"
#include "linux.h"
#include <vxl/v2/startup.h>
#include <vxl/v2/control.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include "cdma_wait.h"
struct startup_state {
 struct vxl_v2_linux adapter;struct vxl_v2_ctx ctx;struct vxl_v2_hw hw;
 struct vxl_v2_catalog catalog;struct vxl_v2_program_table programs;
 struct vxl_v2_queue queue;struct vxl_v2_execution execution;
 struct vxl_v2_controls controls;struct vxl_v2_file_blob catalog_file,execution_file;
 struct vxl_v2_cdma_wait cdma_wait;
};
static struct vxl_device *device(void *p){return p;}
static int held(void *p){struct vxl_device *d=device(p);lockdep_assert_held(&d->mutex);return mutex_is_locked(&d->mutex);}
static int ready(void *p){return gpiod_get_value_cansleep(device(p)->ready);}
static vxl_u32 read_gpu(void *p,vxl_u32 o){return readl(device(p)->gpu+o);}
static void write_gpu(void *p,vxl_u32 o,vxl_u32 v){writel(v,device(p)->gpu+o);}
static vxl_u32 read_cdma(void *p,vxl_u32 o){struct startup_state *s=p;return readl(device(s->hw.os)->cdma+o);}
static void write_cdma(void *p,vxl_u32 o,vxl_u32 v){struct startup_state *s=p;if(o==0x28){vxl_v2_cdma_wait_begin(&s->cdma_wait,ktime_get_ns());dma_wmb();}writel(v,device(s->hw.os)->cdma+o);}
static vxl_u64 clock_ns(void *p){(void)p;return ktime_get_ns();}
static vxl_u64 clock_ms(void *p){return clock_ns(p)/1000000;}
/* V2 transaction-local budget, independent of every legacy profile condition.
 * Common CDMA code still checks IOC/errors on every poll and its old timeout. */
static void pause_cdma(void *p){struct startup_state *s=p;if(vxl_v2_cdma_wait_short(&s->cdma_wait,ktime_get_ns()))udelay(1);else usleep_range(1000,2000);}
static void memory_order(void *p,int write){(void)p;if(write)dma_wmb();else dma_rmb();}
static int raw_snapshot(void *p,vxl_u64 seq,struct vxl_v2_snapshot *out){return vxl_v2_execution_snapshot(p,seq,out);}
static int linux_snapshot(struct vxl_v2_linux *l,vxl_u64 seq,struct vxl_v2_snapshot *out){struct startup_state *s=container_of(l,struct startup_state,adapter);return raw_snapshot(&s->execution,seq,out);}
static int property_sha(struct device_node *node,const char *name,vxl_u8 out[32]){
 int bytes;const void *p=of_get_property(node,name,&bytes);if(!p || bytes!=32)return -EINVAL;memcpy(out,p,32);return 0;
}
static void free_state(void *p){struct startup_state *s=p;if(s->catalog_file.bytes)vxl_v2_linux_files.release(0,&s->catalog_file);if(s->execution_file.bytes)vxl_v2_linux_files.release(0,&s->execution_file);kvfree(s);}
int vxl_v2_linux_startup(struct vxl_device *d){
 struct startup_state *s;struct device_node *node=d->dev->of_node;struct vxl_v2_caps *caps;const char *compatible;vxl_u8 cat_sha[32],exec_sha[32];int rc;
 if(!of_property_read_bool(node,"vortex,compute-v2") || of_property_count_strings(node,"compatible")!=1)return -EINVAL;
 rc=of_property_read_string(node,"compatible",&compatible);if(rc)return rc;
 s=kvzalloc(sizeof(*s),GFP_KERNEL);if(!s)return -ENOMEM;
 if(strlen(compatible)>=sizeof(s->hw.identity.compatible)){rc=-EINVAL;goto failed;}
 strcpy(s->hw.identity.compatible,compatible);
 if(!strcmp(compatible,"vortex,hw4-s8-demo-v1"))s->hw.identity.provider.flags=1;
 else if(!strcmp(compatible,"vortex,w4-graphics-p8-v1"))s->hw.identity.provider.flags=2;
 else if(!strcmp(compatible,"vortex,hw4-s8-joint-v1"))s->hw.identity.provider.flags=4;
 else {rc=-ENODEV;goto failed;}
 rc=property_sha(node,"vortex,hardware-contract-sha256",s->hw.identity.provider.hardware_contract);if(rc)goto failed;
 rc=property_sha(node,"vortex,bit-sha256",s->hw.identity.provider.bit);if(rc)goto failed;
 rc=property_sha(node,"vortex,xsa-sha256",s->hw.identity.provider.xsa);if(rc)goto failed;
 rc=property_sha(node,"vortex,catalog-sha256",cat_sha);if(rc)goto failed;
 rc=property_sha(node,"vortex,execution-sha256",exec_sha);if(rc)goto failed;
 s->hw.ctx=&s->ctx;s->hw.ps=d->ps;s->hw.ps_dma=d->ps_dma;s->hw.os=d;s->hw.locked=held;s->hw.ready=ready;s->hw.read=read_gpu;s->hw.write=write_gpu;s->hw.memory_barrier=memory_order;s->hw.now_ns=clock_ns;
 s->hw.cdma=(struct vxl_cdma_io){s,read_cdma,write_cdma,clock_ms,pause_cdma,0};
 vxl_v2_init(&s->ctx,&vxl_v2_native_memory_ops,&s->hw);s->ctx.guard_bytes=64;
 rc=vxl_v2_read_catalog_file(&vxl_v2_linux_files,&s->hw.identity.provider,&s->catalog,&s->catalog_file);if(rc)goto failed;
 rc=vxl_v2_linux_files.read(0,"/lib/firmware/vxl/execution-v1.bin",4160,&s->execution_file);if(rc)goto failed;
 if(!s->execution_file.regular || !s->execution_file.no_follow || s->execution_file.uid || (s->execution_file.mode&0022)){rc=-EACCES;goto failed;}
 mutex_lock(&d->mutex);
 rc=vxl_v2_boot_bind(&s->execution,&s->hw,&s->catalog,s->catalog_file.bytes,s->catalog_file.length,s->execution_file.bytes,s->execution_file.length,cat_sha,exec_sha);
 if(!rc)rc=vxl_v2_table_bind(&s->ctx,&s->programs,&s->catalog);
 if(!rc)rc=vxl_v2_queue_bind(&s->ctx,&s->queue,&vxl_v2_native_execution_ops,&s->execution);
 if(!rc){s->queue.snapshot=raw_snapshot;s->queue.metrics=&s->execution.last;s->queue.fast_supported=1;rc=vxl_v2_linux_bind(d,&s->adapter,&s->ctx,linux_snapshot);}
 mutex_unlock(&d->mutex);if(rc)goto failed;
 caps=&s->controls.caps;caps->version=2;caps->bytes=256;caps->flags=VXL_V2_CAP_FAST_RUNTIME;caps->feature_mask=s->hw.identity.provider.flags;caps->identity_source=VXL_V2_ID_BOOT_MANIFEST_DECLARED;
 memcpy(caps->hardware_contract,s->hw.identity.provider.hardware_contract,32);memcpy(caps->bit,s->hw.identity.provider.bit,32);memcpy(caps->xsa,s->hw.identity.provider.xsa,32);vxl_hunet_hash(compatible,strlen(compatible),caps->compatible);
 caps->ps_base=VXL_PS_BASE;caps->ps_bytes=VXL_PS_SIZE;caps->pl_base=VXL_PL_BASE;caps->pl_bytes=VXL_PL_SIZE;
 caps->max_objects=64;caps->max_pending=32;caps->max_completions=64;caps->max_refs=8;caps->max_args=4096;caps->max_programs=16;caps->cycle_bits=32;caps->contexts=1;
 s->controls.files=&vxl_v2_linux_files;s->adapter.port.control=vxl_v2_control;s->adapter.port.control_context=&s->controls;
 rc=devm_add_action_or_reset(d->dev,free_state,s);if(rc){d->compute=0;return rc;}
 dev_info(d->dev,"compute-v2 startup provider=%u catalog_entries=%u boot_manifest_declared=1 hardware_hash_measured=0\n",caps->feature_mask,s->catalog.count);return 0;
failed:free_state(s);return rc;
}

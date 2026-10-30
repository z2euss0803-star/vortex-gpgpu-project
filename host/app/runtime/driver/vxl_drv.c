#include "vxl_internal.h"
#include "v2/linux.h"
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/sizes.h>
static void worker(struct work_struct *work){struct vxl_device *d=container_of(to_delayed_work(work),struct vxl_device,worker);struct vxl_submit_time *t;u64 begin;int running;mutex_lock(&d->mutex);if(d->compute)vxl_v2_worker_locked(&d->compute->port);else{t=d->submit_trace.active;running=d->engine.job.state==VXL_RUNNING;begin=ktime_get_ns();vxl_engine_poll(&d->engine);if(d->engine.s8&&running)vxl_s8_timing_add(d->engine.s8,VXL_S8_WORKER_POLL,begin,ktime_get_ns(),0);if(t){vxl_time_add(t,&t->worker_poll_ns,begin,ktime_get_ns());t->worker_poll_calls++;}}mutex_unlock(&d->mutex);schedule_delayed_work(&d->worker,msecs_to_jiffies(5));}
static int probe_error(struct device *dev,const char *stage,int rc) {
 if(rc==-EPROBE_DEFER)dev_dbg(dev,"probe stage=%s deferred rc=%d\n",stage,rc);
 else dev_err(dev,"probe stage=%s failed rc=%d\n",stage,rc);
 return rc;
}
static int probe(struct platform_device *pdev) {
 struct vxl_device *d;struct resource *gpu,*cdma;int rc;const char *stage;u32 id,config;bool demo=of_device_is_compatible(pdev->dev.of_node,"vortex,hw4-s8-demo-v1");bool unified=of_property_read_bool(pdev->dev.of_node,"vortex,compute-v2");
 if((of_device_is_compatible(pdev->dev.of_node,"vortex,w4-graphics-p8-v1") || of_device_is_compatible(pdev->dev.of_node,"vortex,hw4-s8-joint-v1")) && !unified)return probe_error(&pdev->dev,"p8-requires-v2-binding",-ENODEV);
 gpu=platform_get_resource(pdev,IORESOURCE_MEM,0);cdma=platform_get_resource(pdev,IORESOURCE_MEM,1);
 if(!gpu||!cdma||gpu->start!=0xa0000000ULL||cdma->start!=0xb0010000ULL||resource_size(gpu)!=SZ_64K||resource_size(cdma)!=SZ_64K)return probe_error(&pdev->dev,"resources",-ENODEV);
 /* No generic driver may own the same CDMA range. devm resource request enforces this. */
 d=devm_kzalloc(&pdev->dev,sizeof(*d),GFP_KERNEL);if(!d)return probe_error(&pdev->dev,"state-allocation",-ENOMEM);d->dev=&pdev->dev;
 d->ready=devm_gpiod_get(d->dev,"ready",GPIOD_IN);if(IS_ERR(d->ready))return probe_error(d->dev,"ready-gpio",PTR_ERR(d->ready));
 d->nclks=devm_clk_bulk_get_all(d->dev,&d->clks);if(d->nclks<=0)return probe_error(d->dev,"clock-acquire",d->nclks?d->nclks:-ENODEV);
 if(!unified){d->reset=devm_reset_control_get_shared(d->dev,NULL);if(IS_ERR(d->reset))return probe_error(d->dev,"reset-acquire",PTR_ERR(d->reset));}
 /* Approved manifest property alone is insufficient without exact reserved DMA placement. */
 d->approved=of_property_read_bool(d->dev->of_node,"vortex,ps-placement-approved");
 if(!d->approved)return probe_error(d->dev,"placement-approval",-EOPNOTSUPP);
 /* Linux DMA32 support is distinct from our narrower fixed PS allocation pool. */
 rc=dma_set_mask_and_coherent(d->dev,DMA_BIT_MASK(32));if(rc)return probe_error(d->dev,"dma-mask-32",rc);
 rc=of_reserved_mem_device_init(d->dev);if(rc)return probe_error(d->dev,"reserved-memory-attach",rc);
 stage="coherent-allocation";
 d->ps=dma_alloc_coherent(d->dev,VXL_PS_SIZE,&d->ps_dma,GFP_KERNEL);if(!d->ps){rc=-ENOMEM;goto detach;}
 stage="coherent-placement";
 if(d->ps_dma!=VXL_PS_BASE){dev_err(d->dev,"PS DMA placement expected=0x%llx actual=%pad\n",(unsigned long long)VXL_PS_BASE,&d->ps_dma);rc=-ERANGE;goto free_ps;}
 stage="clock-enable";
 rc=clk_bulk_prepare_enable(d->nclks,d->clks);if(rc)goto free_ps;
 if(!unified){stage="reset-deassert";rc=reset_control_deassert(d->reset);if(rc)goto clocks;}
 stage="ready-value";
 if(gpiod_get_value_cansleep(d->ready)!=1){rc=-EAGAIN;goto reset;}
 stage="gpu-map";d->gpu=devm_ioremap_resource(d->dev,gpu);if(IS_ERR(d->gpu)){rc=PTR_ERR(d->gpu);goto reset;}
 stage="cdma-map";d->cdma=devm_ioremap_resource(d->dev,cdma);if(IS_ERR(d->cdma)){rc=PTR_ERR(d->cdma);goto reset;}
 stage="gpu-profile";id=readl(d->gpu);config=readl(d->gpu+4);
 rc=vxl_probe_check(1,1,1,1,id,config,0);if(rc){dev_err(d->dev,"GPU profile id=0x%08x config=0x%08x\n",id,config);goto reset;}
 stage="gpu-abi";
 if(readl(d->gpu+0x30)!=15||readl(d->gpu+0x38)!=0x10001){rc=-ENODEV;goto reset;}
 mutex_init(&d->mutex);INIT_DELAYED_WORK(&d->worker,worker);
 if(unified){stage="compute-v2-startup-binding";rc=vxl_v2_linux_startup(d);if(rc)goto reset;goto publish;}
 stage="engine-profile-init";
 rc=vxl_engine_init_profile(&d->engine,&vxl_hw_ops,d,1,(demo||of_device_is_compatible(d->dev->of_node,"vortex,hw4-s8-validation-v1"))?5:of_device_is_compatible(d->dev->of_node,"vortex,hw4-hunet-v5")?4:1);if(rc)goto reset;
 if(d->engine.profile_id==5){stage="s8-release-attach";rc=vxl_s8_attach(d);if(rc)goto reset;}
 if(d->engine.profile_id==5){stage="s8-evidence-traces";rc=vxl_s8_tracefs_init(d);if(rc)goto reset;}
 if(d->engine.profile_id==4){stage="hunet-release-attach";rc=vxl_hunet_attach(d);if(rc)goto reset;}
 if(d->engine.profile_id==4){stage="submit-timing-diagnostics";rc=vxl_submit_trace_init(d);if(rc)goto reset;}
 if(d->engine.profile_id==4&&d->hunet.opt.release){stage="management-timing-diagnostics";rc=vxl_mgmt_trace_init(d);if(rc)goto reset;}
 if(demo){stage="demo-profile-coordinator";rc=vxl_profile_setup(d);if(rc)goto reset;
  stage="demo-hunet-submit-traces";rc=vxl_submit_trace_init(d);if(rc)goto reset;
  stage="demo-hunet-management-traces";rc=vxl_mgmt_trace_init(d);if(rc)goto reset;
 }
 /* Permanent internal bounce block never available to public allocation. */
 if(d->engine.profile_id==1){stage="bounce-reserve";rc=vxl_pool_reserve(&d->engine.pools[0],VXL_PS_SIZE-4096,4096);if(rc)goto reset;}
publish:
 d->misc.minor=MISC_DYNAMIC_MINOR;d->misc.name="vortex0";d->misc.fops=&vxl_fops;d->misc.parent=d->dev;
 stage="misc-register";rc=misc_register(&d->misc);if(rc)goto reset;platform_set_drvdata(pdev,d);schedule_delayed_work(&d->worker,msecs_to_jiffies(5));dev_info(d->dev,"backend ready dma_bits=32 ps_dma=%pad gpu_id=0x%08x config=0x%08x\n",&d->ps_dma,id,config);return 0;
reset:
 /* A v2 candidate never resets live hardware or retries an uncertain bind.
  * Preserve resources/evidence, expose no device and schedule no worker. */
 if(unified){platform_set_drvdata(pdev,d);dev_err(d->dev,"compute-v2 quarantined stage=%s rc=%d; no misc/worker/reset; external recovery required\n",stage,rc);return 0;}
 reset_control_assert(d->reset);
clocks: clk_bulk_disable_unprepare(d->nclks,d->clks);
free_ps: dma_free_coherent(d->dev,VXL_PS_SIZE,d->ps,d->ps_dma);
detach: of_reserved_mem_device_release(d->dev);return probe_error(d->dev,stage,rc);
}
static const struct of_device_id matches[]={{.compatible="vortex,hw4-s8-joint-v1"},{.compatible="vortex,w4-graphics-p8-v1"},{.compatible="vortex,hw4-s8-demo-v1"},{.compatible="vortex,h0-r4-v1"},{.compatible="vortex,hw4-hunet-v5"},{.compatible="vortex,hw4-s8-validation-v1"},{}};
MODULE_DEVICE_TABLE(of,matches);
static struct platform_driver vxl_driver={.probe=probe,.driver={.name="vxl-linux",.of_match_table=matches,.suppress_bind_attrs=true}};
/* Deliberately no module_exit/remove: offline module builds are not unloadable deployments. */
static int __init vxl_init(void){return platform_driver_register(&vxl_driver);}
module_init(vxl_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("H0 exclusive backend; real PS placement and kernel approval gates remain closed");

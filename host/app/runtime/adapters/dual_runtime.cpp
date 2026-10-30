#include <vxl/demo_dual_runtime.h>
#include <vxl/demo_frame.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
namespace {
int owner(void *){int fd=open("/dev/vortex0",O_RDWR|O_CLOEXEC|O_NOFOLLOW);return fd<0?-errno:fd;}
int close_owner(void *,int fd){return close(fd)<0?-errno:0;}
int display(void *p,const demo_result *result,demo_layout layout){auto &runtime=*static_cast<demo_dual_runtime*>(p);const auto &cfg=*runtime.config;demo_frame frame{};int rc=demo_prepare_display(cfg.store,result,layout,&frame);if(!rc)rc=demo_frame_publish(cfg.store,&frame,result);if(!rc)rc=demo_send_display_reference(cfg.store,cfg.viewer_socket,&frame);demo_frame_free(&frame);return rc;}
}
int demo_dual_runtime_init(demo_dual_runtime *d,demo_session *s,const demo_dual_config *c){
 if(!d)return -EINVAL;
 if(d->config||d->dispatch.session||d->hunet.state)return -EBUSY;
 if(!s||!c||!c->store||!c->mosaic_program||!c->mosaic_program->assets||!c->mosaic_assets||!c->mosaic_input||!c->mosaic_output||!c->hunet||c->hunet->store!=c->store||!c->viewer_socket||c->mosaic_layout>=c->mosaic_program->layout_count)return -EINVAL;
 const auto &program=*c->mosaic_program;const auto &layout=program.layouts[c->mosaic_layout].layout;
 if(c->mosaic_frame_bytes!=size_t(layout.width)*layout.height*12||c->mosaic_assets_bytes!=program.assets_bytes||mosaic_assets_validate(c->mosaic_assets,c->mosaic_assets_bytes,program.assets)||memcmp(program.binding.digests[MOSAIC_HARDWARE_SHA],c->hunet->hardware_sha,32))return -ESTALE;
#ifndef DEMO_SOFTWARE_MODEL_TEST
 if(strcmp(c->viewer_socket,"/run/vxl-demo/dp.sock"))return -EPERM;
#endif
 *d={};d->config=c;int rc=demo_hunet_adapter_init(&d->hunet,c->hunet);if(rc){*d={};return rc;}
 auto &a=d->mosaic;a.store=c->store;a.program=c->mosaic_program;a.assets=c->mosaic_assets;a.assets_bytes=c->mosaic_assets_bytes;a.input=c->mosaic_input;a.output=c->mosaic_output;a.frame_bytes=c->mosaic_frame_bytes;a.layout=c->mosaic_layout;
 auto &dispatch=d->dispatch;rc=demo_mosaic_ops(&a,&dispatch.apps[0]);if(!rc)rc=demo_hunet_adapter_ops(&d->hunet,&dispatch.apps[1]);dispatch.detach[0]=demo_mosaic_adapter_detach;dispatch.detach[1]=demo_hunet_adapter_detach;dispatch.control={owner,close_owner,demo_dispatch_posix_info,demo_dispatch_posix_select,display,d};
 if(!rc)rc=demo_dispatch_init(s,c->store,1,&dispatch);
 if(rc){(void)demo_hunet_adapter_destroy(&d->hunet);*d={};return rc;}return 0;
}
int demo_dual_runtime_destroy(demo_dual_runtime *d){if(!d||!d->dispatch.session)return -EINVAL;auto *session=d->dispatch.session;int rc=demo_owner_release(session);if(!rc)rc=demo_hunet_adapter_destroy(&d->hunet);if(rc)return rc;session->dispatcher=nullptr;session->gpu={};*d={};return 0;}

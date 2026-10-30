#include <vxl/profile_switch.h>
#include <vxl/hunet.h>
#include <vxl/s8.h>
#include <vxl/s8_trace.h>
#include <vxl/mosaic_runtime.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#define VXL_INTERNAL_H
struct vxl_device {struct vxl_engine engine;struct mosaic_ctx mosaic;struct mosaic_host_state mosaic_state;struct vxl_s8_ctx s8;struct vxl_s8_trace s8_trace;struct vxl_hunet_ctx hunet;struct vxl_hunet_release release;struct vxl_hybrid_release hybrid_release;struct vxl_fused_release fused_release;struct vxl_opt_release opt_release;struct vxl_profile_factory profile_factory;struct vxl_device *profile_candidate;void *gpu,*cdma;int *ready;};
static int fail_alloc,fail_attach,allocations,frees;
void *test_zalloc(size_t n,int flags){(void)flags;if(fail_alloc)return NULL;++allocations;return calloc(1,n);}
void test_free(void *p){if(p)++frees;free(p);}
#define kzalloc test_zalloc
#define kvzalloc test_zalloc
#define kfree test_free
#define kvfree test_free
#define GFP_KERNEL 0
#define readl(p) (*(vxl_u32*)(p))
#define gpiod_get_value_cansleep(p) (*(p))
int vxl_s8_candidate(struct vxl_device *d,struct vxl_engine *e,struct vxl_s8_ctx *c){(void)d;if(fail_attach)return -ESTALE;memset(c,0,sizeof(*c));c->engine=e;e->s8=c;return 0;}
int vxl_mosaic_candidate(struct vxl_device *d,struct vxl_engine *e,struct mosaic_ctx *c,struct mosaic_host_state *h){(void)d;if(fail_attach)return -ESTALE;memset(c,0,sizeof(*c));memset(h,0,sizeof(*h));c->host=h;h->engine=e;e->mosaic=c;return 0;}
int vxl_hunet_candidate(struct vxl_device *d,struct vxl_device *stage,void *snapshot){(void)d;if(fail_attach)return -ESTALE;memset(&stage->hunet,0,sizeof(stage->hunet));stage->hunet.engine=&stage->engine;stage->hunet.release=&stage->release;stage->hunet.input_snapshot=snapshot;stage->hunet.program_id=6;stage->hunet.fused.release=&stage->fused_release;stage->engine.hunet=&stage->hunet;return 0;}
#include "../../driver/vxl_profile.c"
int main(void){struct vxl_device *d=calloc(1,sizeof(*d));vxl_u32 gpu[64]={0},cdma[4]={0};int ready=1;d->gpu=gpu;d->cdma=cdma;d->ready=&ready;gpu[0]=0x56580204;gpu[1]=0x40404;gpu[0x30/4]=15;gpu[0x34/4]=1;gpu[0x38/4]=0x10001;cdma[1]=0x2; /* idle before any transfer: IOC is not a precondition */
 assert(!vxl_engine_init_profile(&d->engine,NULL,d,9,5));d->engine.s8=&d->s8;d->s8.engine=&d->engine;d->engine.owner=77;d->engine.opened=1;assert(!vxl_profile_setup(d));
 assert(!profile_idle(d,&d->engine));
 assert(vxl_cdma_status(2)==0); /* Not transfer-complete: strict IOC gate unchanged. */
 cdma[1]=0x1000;assert(profile_idle(d,&d->engine)==-EBUSY);
 cdma[1]=0;assert(profile_idle(d,&d->engine)==-EBUSY);
 for(unsigned bit=0;bit<16;bit++)if(0x4770u&(1u<<bit)){cdma[1]=2u|(1u<<bit);assert(profile_idle(d,&d->engine)==-EBUSY);}
 cdma[1]=0x1002;assert(!profile_idle(d,&d->engine));assert(vxl_cdma_status(cdma[1])==1);cdma[1]=2;
 struct vxl_request r={.abi_major=1,.struct_size=176,.profile_id=6,.generation=9};assert(!vxl_profile_select(d,77,&r));assert(d->engine.mosaic==&d->mosaic&&d->mosaic.host==&d->mosaic_state&&d->mosaic_state.engine==&d->engine&&d->engine.generation==10&&d->mosaic_state.owner==77);
 struct vxl_engine saved=d->engine;fail_alloc=1;r=(struct vxl_request){.abi_major=1,.struct_size=176,.profile_id=4,.generation=10};assert(vxl_profile_select(d,77,&r)==-ENOMEM&&!memcmp(&saved,&d->engine,sizeof(saved)));fail_alloc=0;fail_attach=1;int before=allocations,freed=frees;assert(vxl_profile_select(d,77,&r)==-ESTALE&&!memcmp(&saved,&d->engine,sizeof(saved))&&allocations-before==frees-freed);fail_attach=0;
 assert(!vxl_profile_select(d,77,&r)&&d->engine.hunet==&d->hunet&&d->hunet.input_snapshot&&d->hunet.release==&d->release&&d->hunet.engine==&d->engine);r=(struct vxl_request){.abi_major=1,.struct_size=176,.profile_id=6,.generation=11};assert(!vxl_profile_select(d,77,&r)&&!d->hunet.input_snapshot&&d->engine.mosaic==&d->mosaic&&!d->engine.hunet);assert(allocations==frees);ready=0;r=(struct vxl_request){.abi_major=1,.struct_size=176,.profile_id=5,.generation=12};assert(vxl_profile_select(d,77,&r)<0&&d->engine.profile_id==6);free(d);puts("ACTUAL_DRIVER_PROFILE_FACTORY_PRECONSTRUCT_REBASE_ROLLBACK_NO_RESET_HOST_PASS_NOT_KERNEL_NOT_BOARD");return 0;}

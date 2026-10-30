#include <vxl/profile_switch.h>
#include <vxl/hunet.h>
#include <vxl/s8.h>
#include <vxl/mosaic_host.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
static struct vxl_engine *candidate;static struct vxl_s8_ctx s8a,s8b;static struct vxl_hunet_ctx hunet;static struct mosaic_ctx mosaic;static struct mosaic_host_state host;static int bad_prepare,bad_idle,prepared,committed,discarded;
static int idle(void *p,struct vxl_engine *e){(void)p;(void)e;return bad_idle?-EIO:0;}
static int prepare(void *p,vxl_u32 target,vxl_u32 gen,struct vxl_engine **out){(void)p;++prepared;if(bad_prepare)return -ENOMEM;candidate=calloc(1,sizeof(*candidate));assert(candidate);assert(!vxl_engine_init_profile(candidate,NULL,NULL,gen,target));if(target==5){memset(&s8b,0,sizeof(s8b));s8b.engine=candidate;candidate->s8=&s8b;}if(target==4){memset(&hunet,0,sizeof(hunet));hunet.engine=candidate;candidate->hunet=&hunet;}if(target==6){memset(&mosaic,0,sizeof(mosaic));memset(&host,0,sizeof(host));mosaic.host=&host;host.engine=candidate;candidate->mosaic=&mosaic;}*out=candidate;return 0;}
static void discard(void *p,struct vxl_engine *c){(void)p;++discarded;free(c);candidate=NULL;}
static void commit(void *p,struct vxl_engine *c){struct vxl_engine *e=p;++committed;if(e->s8)e->s8->engine=e;if(e->hunet)e->hunet->engine=e;if(e->mosaic)e->mosaic->host->engine=e;free(c);candidate=NULL;}
int main(void){struct vxl_engine e;assert(!vxl_engine_init_profile(&e,NULL,NULL,9,5));e.owner=77;e.opened=1;e.s8=&s8a;s8a.engine=&e;struct vxl_profile_factory factory={1,&e,idle,prepare,discard,commit};e.profile_factory=&factory;
 assert(!vxl_select_profile(&e,77,6,9));assert(e.profile_id==6&&e.generation==10&&e.owner==77&&e.opened&&e.mosaic&&!e.s8&&!e.hunet&&e.mosaic->host->engine==&e);
 assert(!vxl_select_profile(&e,77,4,10));assert(e.profile_id==4&&e.generation==11&&e.hunet&&!e.s8&&!e.mosaic);
 assert(!vxl_select_profile(&e,77,6,11));assert(e.generation==12&&committed==3);assert(vxl_select_profile(&e,77,5,11)==-ESTALE);
 struct vxl_engine saved=e;bad_prepare=1;assert(vxl_select_profile(&e,77,5,12)==-ENOMEM&&!memcmp(&saved,&e,sizeof(e)));bad_prepare=0;bad_idle=1;int count=prepared;assert(vxl_select_profile(&e,77,5,12)==-EIO&&prepared==count&&!memcmp(&saved,&e,sizeof(e)));bad_idle=0;
 e.objects[0].handle=123;assert(vxl_select_profile(&e,77,5,12)==-EBUSY);e.objects[0].handle=0;e.job.state=VXL_RUNNING;assert(vxl_select_profile(&e,77,5,12)==-EBUSY);e.job.state=VXL_READY;host.evidence_pending=1;assert(vxl_select_profile(&e,77,5,12)==-EBUSY);host.evidence_pending=0;e.quarantined=1;e.job.pinned=1;assert(vxl_select_profile(&e,77,5,12)<0);e.quarantined=e.job.pinned=0;e.generation=0xffffffff;assert(vxl_select_profile(&e,77,5,0xffffffff)==-EOVERFLOW);e.generation=12;factory.enabled=0;assert(vxl_select_profile(&e,77,5,12)==-ENOTTY);factory.enabled=1;
 struct vxl_request r={.abi_major=1,.struct_size=176,.generation=12,.profile_id=5};r.length=1;assert(vxl_select_request(&e,77,&r)==-EINVAL);r.length=0;assert(!vxl_select_request(&e,77,&r)&&r.generation==13&&r.profile_id==5);assert(e.s8&&!e.hunet&&!e.mosaic&&e.owner==77);assert(!discarded);puts("PROFILE_PRECONSTRUCT_COMMIT_FAILURE_PRESERVATION_AND_176B_SELECT_HOST_PASS_NOT_BOARD");return 0;}

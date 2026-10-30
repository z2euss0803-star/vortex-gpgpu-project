/* Dispatch regression fixture only; not a GPU/backend execution model. */
#include <callbacks.h>
#include <cstdlib>
#include <cstdio>
static void trace(const char *s){const char *p=getenv("VXL_LEGACY_TRACE");if(p){FILE *f=fopen(p,"a");if(f){fprintf(f,"%s\n",s);fclose(f);}}}
static int open(vx_device_h *d){*d=reinterpret_cast<void *>(1);trace("OPEN");return 0;}
static int close(vx_device_h){trace("CLOSE");return 0;}
static int caps(vx_device_h,uint32_t id,uint64_t *v){*v=id==VX_CAPS_NUM_CORES?1:0;return 0;}
static int dcr(vx_device_h,uint32_t,uint32_t){trace("DCR");return 0;}
static int mpm(vx_device_h,uint32_t,uint32_t,uint64_t *v){trace("MPM");*v=0;return 0;}
extern "C" int vx_dev_init(callbacks_t *c){*c={};c->dev_open=open;c->dev_close=close;c->dev_caps=caps;c->dcr_write=dcr;c->mpm_query=mpm;return 0;}

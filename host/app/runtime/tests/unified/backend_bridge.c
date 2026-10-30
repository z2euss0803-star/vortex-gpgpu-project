#include "backend_bridge.h"
#include "../v2/port_fixture.h"
#include <vxl/v2/control.h>
struct bridge {struct os os;struct vxl_v2_controls controls;int fault_submit,fault_ack;};
static int file_read(void *p,const char *path,vxl_u32 max,struct vxl_v2_file_blob *b){(void)p;(void)path;assert(max==8388608);b->bytes=calloc(1,64);assert(b->bytes);b->length=64;b->mode=0444;b->regular=b->no_follow=1;return 0;}
static void file_release(void *p,struct vxl_v2_file_blob *b){(void)p;free(b->bytes);}
static const struct vxl_v2_file_ops files={0,file_read,file_release};
void *backend_bridge_create(const char *file){struct bridge *b=calloc(1,sizeof(*b));struct vxl_v2_caps *c;assert(b);setup(&b->os.f,file);assert(!pthread_mutex_init(&b->os.mutex,0));b->os.port=(struct vxl_v2_port){.ctx=&b->os.f.c,.ops=&port_ops,.os=&b->os,.control=vxl_v2_control,.control_context=&b->controls};b->controls.files=&files;c=&b->controls.caps;c->version=2;c->bytes=256;c->max_objects=64;c->max_pending=32;c->max_completions=64;c->max_refs=8;c->max_args=4096;c->max_programs=16;c->cycle_bits=32;c->contexts=1;c->feature_mask=1;c->identity_source=1;c->ps_base=VXL_PS_BASE;c->ps_bytes=VXL_PS_SIZE;c->pl_base=VXL_PL_BASE;c->pl_bytes=VXL_PL_SIZE;memset(c->hardware_contract,1,32);memset(c->bit,2,32);memset(c->xsa,3,32);memset(c->compatible,4,32);return b;}
int backend_bridge_call(void *p,unsigned long cmd,void *r){struct bridge *b=p;int rc;
#ifdef VXL_V2_FAST_RUNTIME_API
 b->controls.caps.flags=VXL_V2_CAP_FAST_RUNTIME;b->os.f.q->fast_supported=1;
#endif
 b->os.fault=(b->fault_submit && cmd==VXL_V2_SUBMIT_BATCH) || (b->fault_ack && cmd==VXL_V2_COMPLETE_REAP && ((struct vxl_compute_v2_request *)r)->flags==VXL_V2_ACK);rc=dispatch(&b->os,(unsigned int)cmd,r);b->os.fault=0;if(b->os.f.q->pending && !b->os.f.c.active){lock_os(&b->os);assert(vxl_v2_worker_locked(&b->os.port)==0);unlock_os(&b->os);}return rc;}
void backend_bridge_entry(void *p,unsigned char out[32]){memcpy(out,((struct bridge *)p)->os.f.program.entry_id,32);}
uint64_t backend_bridge_live(void *p){return ((struct bridge *)p)->os.f.c.live;}
unsigned backend_bridge_starts(void *p){return ((struct bridge *)p)->os.f.hw.starts;}
int backend_bridge_staging(void *p){struct bridge *b=p;struct vxl_v2_queue *q=b->os.f.q;if(q->active_slot<0)return 0;for(unsigned i=0;i<64;i++)if(b->os.f.c.objects[i].handle==q->slots[q->active_slot].job.args_handle)return b->os.f.c.objects[i].purpose==2;return 0;}
void backend_bridge_finish(void *p,int value){((struct bridge *)p)->os.finish=value;}
void backend_bridge_interrupt_wait(void *p,int value){((struct bridge *)p)->os.interrupt=value;}
void backend_bridge_fault_submit(void *p,int value){((struct bridge *)p)->fault_submit=value;}
void backend_bridge_fault_ack(void *p,int value){((struct bridge *)p)->fault_ack=value;}
void backend_bridge_identity_change(void *p){((struct bridge *)p)->controls.caps.compatible[0]^=1;}
void backend_bridge_destroy(void *p){struct bridge *b=p;lock_os(&b->os);assert(vxl_v2_close_locked(&b->os.port,1)==0);unlock_os(&b->os);teardown_host_storage(&b->os.f);pthread_mutex_destroy(&b->os.mutex);free(b);}

#include "port_fixture.h"
#include <vxl/v2/control.h>
_Static_assert(sizeof(struct vxl_v2_create_desc)==64,"create descriptor");
_Static_assert(sizeof(struct vxl_v2_buffer_info)==64,"buffer metadata");
_Static_assert(sizeof(struct vxl_v2_copy_desc)==128,"typed copy descriptor");
_Static_assert(sizeof(struct vxl_v2_program_desc)==64,"program descriptor");
static int read_file(void *p,const char *path,vxl_u32 max,struct vxl_v2_file_blob *b){(void)p;assert(strstr(path,"/lib/firmware/vxl/programs/")==path && max==8388608);b->bytes=calloc(1,64);assert(b->bytes);b->length=64;b->uid=0;b->mode=0444;b->regular=b->no_follow=1;return 0;}
static void release_file(void *p,struct vxl_v2_file_blob *b){(void)p;free(b->bytes);}
int main(int argc,char **argv){struct os o={0};struct vxl_v2_controls controls={0};struct vxl_compute_v2_request r;struct vxl_v2_caps caps;struct vxl_v2_create_desc create={.domain=2,.purpose=3,.access=3,.bytes=64};struct vxl_v2_buffer_info first,second;struct vxl_v2_copy_desc copy={0};struct vxl_v2_program_desc program={0};struct vxl_v2_file_ops files={0,read_file,release_file};vxl_u8 input[37],output[37];struct vxl_v2_job j;
 assert(argc==2);setup(&o.f,argv[1]);assert(!pthread_mutex_init(&o.mutex,0));o.port.ctx=&o.f.c;o.port.ops=&port_ops;o.port.os=&o;o.port.control=vxl_v2_control;o.port.control_context=&controls;controls.files=&files;controls.caps.version=2;controls.caps.bytes=256;controls.caps.max_refs=8;
 r=request();r.device_generation=0;r.user_ptr=(vxl_u64)(uintptr_t)&caps;r.payload_bytes=256;
 assert(dispatch(&o,VXL_V2_CAPS,&r)==0 && caps.device_generation==1 && r.device_generation==1 && o.f.c.epoch==1);
 r=request();r.user_ptr=(vxl_u64)(uintptr_t)&create;r.payload_bytes=64;assert(dispatch(&o,VXL_V2_BUFFER_CREATE,&r)==0);memcpy(&first,&create,64);assert(first.ref.length==64 && first.ref.domain==2);
 create=(struct vxl_v2_create_desc){.domain=2,.purpose=3,.access=3,.bytes=64};r=request();r.user_ptr=(vxl_u64)(uintptr_t)&create;r.payload_bytes=64;assert(dispatch(&o,VXL_V2_BUFFER_CREATE,&r)==0);memcpy(&second,&create,64);
 for(unsigned i=0;i<37;i++)input[i]=(vxl_u8)(i+11);
 copy.destination=first.ref;copy.destination.length=37;copy.bytes=37;copy.direction=1;copy.user_ptr=(vxl_u64)(uintptr_t)input;r=request();r.user_ptr=(vxl_u64)(uintptr_t)&copy;r.payload_bytes=128;assert(dispatch(&o,VXL_V2_BUFFER_COPY,&r)==0);
 copy.source=first.ref;copy.source.length=37;copy.destination=second.ref;copy.destination.length=37;copy.direction=3;copy.user_ptr=0;assert(dispatch(&o,VXL_V2_BUFFER_COPY,&r)==0);
 copy.source=second.ref;copy.source.length=37;memset(&copy.destination,0,48);copy.direction=2;copy.user_ptr=(vxl_u64)(uintptr_t)output;assert(dispatch(&o,VXL_V2_BUFFER_COPY,&r)==0 && !memcmp(input,output,37));
 copy.user_ptr=0;copy.destination=copy.source;copy.direction=3;copy.destination.offset=1;assert(dispatch(&o,VXL_V2_BUFFER_COPY,&r)==-EINVAL);
 create=(struct vxl_v2_create_desc){.domain=2,.purpose=1,.access=3,.bytes=64};r=request();r.user_ptr=(vxl_u64)(uintptr_t)&create;r.payload_bytes=64;assert(dispatch(&o,VXL_V2_BUFFER_CREATE,&r)==-EACCES);
 memcpy(program.entry_id,o.f.program.entry_id,32);r=request();r.user_ptr=(vxl_u64)(uintptr_t)&program;r.payload_bytes=64;assert(dispatch(&o,VXL_V2_PROGRAM_LOAD,&r)==0 && program.handle==o.f.program.handle && o.f.table.programs[0].users==2);
 r=request();r.handle=program.handle;r.object_generation=program.object_generation;assert(dispatch(&o,VXL_V2_PROGRAM_RELEASE,&r)==0 && o.f.table.programs[0].users==1);
 /* Pinned data is denied before transfer, even through the new public decoder. */
 j=job(&o.f,1);j.num_refs=1;j.refs[0]=first.ref;assert(vxl_v2_batch_accept(&o.f.c,1,&j,1)==0);
 copy=(struct vxl_v2_copy_desc){.destination=first.ref,.bytes=64,.direction=1,.user_ptr=(vxl_u64)(uintptr_t)input};r=request();r.user_ptr=(vxl_u64)(uintptr_t)&copy;r.payload_bytes=128;
 assert(dispatch(&o,VXL_V2_BUFFER_COPY,&r)==-EBUSY);o.finish=1;lock_os(&o);assert(vxl_v2_worker_locked(&o.port)==0);assert(vxl_v2_worker_locked(&o.port)==0);assert(vxl_v2_ack_reap(&o.f.c,1,0,1)==0);unlock_os(&o);
 lock_os(&o);assert(vxl_v2_close_locked(&o.port,1)==0);unlock_os(&o);teardown_host_storage(&o.f);pthread_mutex_destroy(&o.mutex);
 puts("V2_BOOTSTRAP_CAPS_TYPED_COPY_CATALOG_AND_PIN_ADMISSION_PASS_NOT_BOARD");return 0;
}

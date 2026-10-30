#include <callbacks.h>
#include <VX_types.h>
#include <vxl/transport.h>
#include <mutex>
#include <unordered_map>
#include <cstdlib>
#include <cerrno>
#include <cstring>
namespace {
std::mutex lock;
struct Buffer {uint64_t handle,size;uint32_t address;int flags;};
struct Device {vxl_transport transport{};uint64_t job=0;std::unordered_map<uintptr_t,Buffer> buffers;std::unordered_map<uint32_t,uint32_t> dcr;};
Device *active=nullptr;uintptr_t device_token=0,serial=0;
vxl_request request(){vxl_request r{};r.abi_major=1;r.struct_size=sizeof(r);return r;}
int call(uint32_t cmd,vxl_request &r){return vxl_transport_call(&active->transport,cmd,&r,sizeof(r));}
bool device(vx_device_h d){return active&&reinterpret_cast<uintptr_t>(d)==device_token;}
Buffer *buffer(vx_buffer_h h){if(!active)return nullptr;auto it=active->buffers.find(reinterpret_cast<uintptr_t>(h));return it==active->buffers.end()?nullptr:&it->second;}
int open_dev(vx_device_h *out){std::lock_guard<std::mutex> guard(lock);if(!out)return -EINVAL;if(active)return -EBUSY;
 auto d=new Device;const char *v=getenv("VXL_TRANSPORT");int rc=vxl_transport_open(&d->transport,v&&strcmp(v,"mock")==0);if(rc){delete d;return rc;}
 active=d;device_token=++serial;*out=reinterpret_cast<void *>(device_token);return 0;
}
int close_dev(vx_device_h d){std::lock_guard<std::mutex> guard(lock);if(!device(d))return -EBADF;vxl_transport_close(&active->transport);delete active;active=nullptr;return 0;}
int caps(vx_device_h d,uint32_t id,uint64_t *out){std::lock_guard<std::mutex> guard(lock);if(!device(d)||!out)return -EINVAL;
 switch(id){case VX_CAPS_VERSION:*out=0x0204;break;case VX_CAPS_NUM_THREADS:case VX_CAPS_NUM_WARPS:case VX_CAPS_NUM_CORES:*out=4;break;
 case VX_CAPS_CACHE_LINE_SIZE:*out=64;break;case VX_CAPS_GLOBAL_MEM_SIZE:case VX_CAPS_MEM_BANK_SIZE:*out=VXL_PL_SIZE;break;
 case VX_CAPS_NUM_MEM_BANKS:*out=1;break;case VX_CAPS_LOCAL_MEM_SIZE:*out=0;break;case VX_CAPS_ISA_FLAGS:return -EOPNOTSUPP;
 default:return -EOPNOTSUPP;}return 0;
}
int allocate(vx_device_h d,uint64_t addr,uint64_t n,int flags,vx_buffer_h *out,bool reserve){std::lock_guard<std::mutex> guard(lock);
 if(!device(d)||!out)return -EINVAL;
 if(flags&VX_MEM_PIN_MEMORY)return -EOPNOTSUPP;
 if(flags&~3)return -EINVAL;
 auto r=request();r.length=n;r.alignment=64;r.domain=reserve&&addr<0x80000000ULL?VXL_PS:VXL_PL;
 r.purpose=reserve?VXL_CODE:VXL_DATA;r.address=addr;int rc=call(reserve?VXL_RESERVE:VXL_ALLOC,r);if(rc)return rc;
 uintptr_t token=++serial;active->buffers.emplace(token,Buffer{r.handle,n,r.gpu_address,flags});*out=reinterpret_cast<void *>(token);return 0;
}
int alloc(vx_device_h d,uint64_t n,int f,vx_buffer_h *b){return allocate(d,0,n,f,b,false);}
int reserve(vx_device_h d,uint64_t a,uint64_t n,int f,vx_buffer_h *b){return allocate(d,a,n,f,b,true);}
int free_buffer(vx_buffer_h h){std::lock_guard<std::mutex> guard(lock);auto b=buffer(h);if(!b)return -EBADF;auto r=request();r.handle=b->handle;int rc=call(VXL_FREE,r);if(!rc)active->buffers.erase(reinterpret_cast<uintptr_t>(h));return rc;}
int access(vx_buffer_h h,uint64_t off,uint64_t n,int f){std::lock_guard<std::mutex> guard(lock);auto b=buffer(h);if(!b)return -EBADF;if(f&~3)return -EINVAL;
 auto r=request();int rc=call(VXL_GET_STATUS,r);if(rc)return rc;if(r.state==VXL_RUNNING)return -EBUSY;if(r.state==VXL_QUARANTINED)return -EIO;
 rc=vxl_range_check(b->size,off,n);if(rc)return rc;b->flags=f;return 0; /* Metadata only, no hardware protection. */
}
int address(vx_buffer_h h,uint64_t *out){std::lock_guard<std::mutex> guard(lock);auto b=buffer(h);if(!b||!out)return -EBADF;*out=b->address;return 0;}
int info(vx_device_h d,uint64_t *free,uint64_t *used){std::lock_guard<std::mutex> guard(lock);if(!device(d)||!free||!used)return -EINVAL;auto r=request();int rc=call(VXL_GET_INFO,r);if(!rc){*used=r.used;*free=r.capacity-r.used;}return rc;}
int copy(vx_buffer_h h,void *p,uint64_t off,uint64_t n,bool write){std::lock_guard<std::mutex> guard(lock);auto b=buffer(h);if(!b)return -EBADF;auto r=request();r.handle=b->handle;r.offset=off;r.length=n;r.user_ptr=reinterpret_cast<uintptr_t>(p);return call(write?VXL_COPY_TO:VXL_COPY_FROM,r);}
int to(vx_buffer_h h,const void *p,uint64_t off,uint64_t n){return copy(h,const_cast<void *>(p),off,n,true);}
int from(void *p,vx_buffer_h h,uint64_t off,uint64_t n){return copy(h,p,off,n,false);}
int device_copy(vx_buffer_h dst,uint64_t off,vx_buffer_h src,uint64_t so,uint64_t n){std::lock_guard<std::mutex> guard(lock);auto a=buffer(dst),b=buffer(src);if(!a||!b)return -EBADF;auto r=request();r.handle=a->handle;r.handle2=b->handle;r.offset=off;r.offset2=so;r.length=n;return call(VXL_COPY_DEVICE,r);}
int start(vx_device_h d,vx_buffer_h kernel,vx_buffer_h args){std::lock_guard<std::mutex> guard(lock);if(!device(d))return -EBADF;auto a=buffer(kernel),b=buffer(args);if(!a||!b)return -EBADF;
 if(active->job){auto reap=request();reap.job_id=active->job;int rc=call(VXL_REAP,reap);if(rc)return rc;active->job=0;}
 auto r=request();r.handle=a->handle;r.handle2=b->handle;r.deadline_ms=10000;r.trust_id=1;int rc=call(VXL_SUBMIT,r);if(!rc)active->job=r.job_id;return rc;
}
int wait(vx_device_h d,uint64_t ms){std::lock_guard<std::mutex> guard(lock);if(!device(d))return -EBADF;if(!active->job)return 0;auto r=request();r.job_id=active->job;r.wait_ms=ms;int rc=call(VXL_WAIT,r);return rc?rc:r.result;}
bool known_dcr(uint32_t a){return a==VX_DCR_BASE_STARTUP_ADDR0||a==VX_DCR_BASE_STARTUP_ADDR1||a==VX_DCR_BASE_STARTUP_ARG0||a==VX_DCR_BASE_STARTUP_ARG1||a==VX_DCR_BASE_MPM_CLASS;}
int dcr_write(vx_device_h d,uint32_t a,uint32_t value){std::lock_guard<std::mutex> guard(lock);if(!device(d))return -EBADF;if(!known_dcr(a))return -EOPNOTSUPP;
 if((a==VX_DCR_BASE_STARTUP_ADDR1||a==VX_DCR_BASE_STARTUP_ARG1)&&value)return -ERANGE;
 if(a==VX_DCR_BASE_MPM_CLASS&&value)return -EOPNOTSUPP;
 auto r=request();int rc=call(VXL_GET_STATUS,r);if(rc)return rc;if(r.state==VXL_RUNNING)return -EBUSY;if(r.state==VXL_QUARANTINED)return -EIO;
 active->dcr[a]=value;return 0;
}
int dcr_read(vx_device_h d,uint32_t a,uint32_t *value){std::lock_guard<std::mutex> guard(lock);if(!device(d)||!value)return -EINVAL;if(!known_dcr(a))return -EOPNOTSUPP;*value=active->dcr[a];return 0;}
int mpm(vx_device_h,uint32_t,uint32_t,uint64_t*){return -EOPNOTSUPP;}
}
extern "C" int vx_dev_init(callbacks_t *c){if(!c)return -EINVAL;*c={open_dev,close_dev,caps,alloc,reserve,free_buffer,access,address,info,to,from,device_copy,start,wait,dcr_read,dcr_write,mpm};return 0;}

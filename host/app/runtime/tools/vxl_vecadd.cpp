#include <vortex.h>
#include "../vendor/h0/vecadd16.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <initializer_list>
int main(int argc,char **argv){
 bool mock=argc==2&&!std::strcmp(argv[1],"--mock");if(mock)setenv("VXL_TRANSPORT","mock",1);else unsetenv("VXL_TRANSPORT");setenv("VORTEX_DRIVER","zynq",1);
 vx_device_h dev=nullptr;vx_buffer_h kernel=nullptr,a=nullptr,b=nullptr,c=nullptr,args=nullptr;
 uint32_t x[64],y[64],out[64],arg[4];uint64_t min=0x10000000,max=min+128,address;unsigned char image[16+sizeof(vecadd16_code)];int rc=vx_dev_open(&dev);if(rc)return 1;
 std::memcpy(image,&min,8);std::memcpy(image+8,&max,8);std::memcpy(image+16,vecadd16_code,sizeof(vecadd16_code));
#define CALL(expr) do {rc=(expr);if(rc)goto done;}while(0)
 CALL(vx_upload_kernel_bytes(dev,image,sizeof(image),&kernel));
 CALL(vx_mem_alloc(dev,256,VX_MEM_READ_WRITE,&a));CALL(vx_mem_alloc(dev,256,VX_MEM_READ_WRITE,&b));CALL(vx_mem_alloc(dev,256,VX_MEM_READ_WRITE,&c));
 for(unsigned i=0;i<64;i++){x[i]=i*7+2;y[i]=i*11+3;}
 CALL(vx_copy_to_dev(a,x,0,256));CALL(vx_copy_to_dev(b,y,0,256));
 arg[0]=64;CALL(vx_mem_address(a,&address));arg[mock?1:0]=(uint32_t)address;
 CALL(vx_mem_address(b,&address));arg[mock?2:1]=(uint32_t)address;CALL(vx_mem_address(c,&address));arg[mock?3:2]=(uint32_t)address;
 CALL(vx_upload_bytes(dev,arg,mock?16:12,&args));CALL(vx_start(dev,kernel,args));CALL(vx_ready_wait(dev,10000));CALL(vx_copy_from_dev(out,c,0,256));
 for(unsigned i=0;i<64;i++)if(out[i]!=18*i+5){rc=-EIO;break;}
done:
 for(auto handle:{args,c,b,a,kernel})if(handle){int free_rc=vx_mem_free(handle);if(!rc&&free_rc)rc=free_rc;}
 {int close_rc=vx_dev_close(dev);if(!rc)rc=close_rc;}
 std::printf("{\"scope\":\"%s\",\"elements\":64,\"result\":%d}\n",mock?"CPU_MOCK_NOT_GPU":"REAL_DRIVER_DIAGNOSTIC",rc);return rc?1:0;
}

#include <vortex.h>
#include <VX_types.h>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <cstdint>
#include <cstdio>
int main() {
 vx_device_h dev=nullptr,second=nullptr;vx_buffer_h kernel=nullptr,a=nullptr,b=nullptr,c=nullptr,args=nullptr;
 uint32_t inA[64],inB[64],out[64],arg[4];uint64_t addr;uint8_t image[32]={};
 uint64_t low=0x10000000,high=0x10000040;
 memcpy(image,&low,8);memcpy(image+8,&high,8);for(unsigned i=16;i<32;i++)image[i]=0x13;
 assert(!vx_dev_open(&dev));assert(vx_dev_open(&second)==-EBUSY);
 assert(vx_mem_alloc(dev,64,VX_MEM_PIN_MEMORY,&a)==-EOPNOTSUPP);
 assert(vx_dcr_write(dev,VX_DCR_BASE_STARTUP_ADDR1,1)==-ERANGE);
 assert(vx_dcr_write(dev,0xffff,0)==-EOPNOTSUPP);
 assert(vx_mpm_query(dev,0,0,&addr)==-EOPNOTSUPP);
 assert(vx_upload_kernel_bytes(dev,image,12,&kernel)!=0);
 assert(!vx_upload_kernel_bytes(dev,image,sizeof(image),&kernel));
 uint8_t loaded[64];assert(!vx_copy_from_dev(loaded,kernel,0,64));
 for(unsigned i=0;i<64;i++)assert(loaded[i]==(i<16?0x13:0));
 assert(!vx_mem_alloc(dev,256,VX_MEM_READ_WRITE,&a));assert(!vx_mem_alloc(dev,256,VX_MEM_READ_WRITE,&b));assert(!vx_mem_alloc(dev,256,VX_MEM_READ_WRITE,&c));
 for(unsigned i=0;i<64;i++){inA[i]=i*2+7;inB[i]=i*3+11;}
 assert(!vx_copy_to_dev(a,inA,0,256));assert(!vx_copy_to_dev(b,inB,0,256));
 arg[0]=64;assert(!vx_mem_address(a,&addr));arg[1]=(uint32_t)addr;
 assert(!vx_mem_address(b,&addr));arg[2]=(uint32_t)addr;assert(!vx_mem_address(c,&addr));arg[3]=(uint32_t)addr;
 assert(!vx_upload_bytes(dev,arg,sizeof(arg),&args));
 assert(!vx_start(dev,kernel,args));assert(vx_mem_free(a)==-EBUSY);
 assert(vx_ready_wait(dev,0)==-ETIMEDOUT);assert(!vx_ready_wait(dev,100));
 assert(!vx_copy_from_dev(out,c,0,256));for(unsigned i=0;i<64;i++)assert(out[i]==5*i+18);
 assert(!vx_mem_free(args));assert(!vx_mem_free(a));assert(vx_mem_free(a)==-EBADF);
 assert(!vx_mem_free(b));assert(!vx_mem_free(c));assert(!vx_mem_free(kernel));assert(!vx_dev_close(dev));
 for(unsigned i=0;i<10;i++){assert(!vx_dev_open(&dev));assert(!vx_dev_close(dev));}
 puts("REAL_PUBLIC_STUB_DYNAMIC_LOAD_SYNTHETIC_VECADD_BSS_PASS");
}

#include <vxl/core.h>
#include <assert.h>
#include <stdio.h>
#include <stddef.h>
int main(void) {
 struct vxl_profile p={VXL_PS_BASE,VXL_PS_SIZE,VXL_PL_BASE,VXL_PL_SIZE};
 struct vxl_pool pool={0}; vxl_u64 x; vxl_u32 gpu;
 struct vxl_request r={0}; struct vxl_job j={0}; struct vxl_event e={0};
 _Static_assert(sizeof(struct vxl_request)==176,"ABI size");
 _Static_assert(offsetof(struct vxl_request,user_ptr)==56,"ABI pointer alignment");
 _Static_assert(offsetof(struct vxl_request,domain)==112,"ABI field layout");
 assert(!vxl_range_check(64,63,1)); assert(vxl_range_check(64,64,1)==-EINVAL);
 assert(vxl_range_check(~0ULL,~0ULL-1,4)==-EOVERFLOW);
 assert(vxl_range_check(64,0,0)==-EINVAL);
 assert(!vxl_addr_encode(&p,VXL_PL,0x500000001ULL,1,&gpu)&&gpu==0x80000001);
 assert(vxl_addr_encode(&p,VXL_PL,0x580000000ULL,1,&gpu)==-ERANGE);
 assert(vxl_addr_encode(&p,VXL_PS,VXL_FENCE,1,&gpu)==-ERANGE);
 pool.capacity=256;
 assert(vxl_pool_alloc(&pool,1,3,&x)==-EINVAL);
 assert(!vxl_pool_alloc(&pool,65,64,&x)&&x==0);
 assert(vxl_pool_reserve(&pool,64,64)==-EBUSY);
 assert(!vxl_pool_alloc(&pool,128,64,&x)&&x==128);
 assert(vxl_pool_alloc(&pool,1,64,&x)==-ENOMEM);
 assert(vxl_pool_free(&pool,0,65)==-EBADF);
 assert(!vxl_pool_free(&pool,0,128)); assert(vxl_pool_free(&pool,0,128)==-EBADF);
 r.abi_major=1; r.struct_size=sizeof(r);
 assert(!vxl_uapi_validate(VXL_GET_INFO,&r,sizeof(r),0));
 assert(vxl_uapi_validate(VXL_GET_INFO,&r,sizeof(r),1)==-EOPNOTSUPP);
 assert(vxl_uapi_validate(123,&r,sizeof(r),0)==-ENOTTY);
 assert(vxl_uapi_validate(VXL_GET_INFO,0,sizeof(r),0)==-EFAULT);
 assert(vxl_uapi_validate(VXL_GET_INFO,&r,sizeof(r)-1,0)==-EINVAL);
 r.flags=1; assert(vxl_uapi_validate(VXL_GET_INFO,&r,sizeof(r),0)==-EINVAL); r.flags=0;
 r.reserved2=1; assert(vxl_uapi_validate(VXL_GET_INFO,&r,sizeof(r),0)==-EINVAL); r.reserved2=0;
 r.abi_major=2; assert(vxl_uapi_validate(VXL_GET_INFO,&r,sizeof(r),0)==-EPROTONOSUPPORT);
 j.state=VXL_READY; j.id=1; e.kind=VXL_START; e.id=1;
 assert(!vxl_job_step(&j,&e)&&j.pinned);
 assert(vxl_job_step(&j,&e)==-EBUSY);
 e.kind=VXL_WAIT_TIMEOUT; assert(vxl_job_step(&j,&e)==-ETIMEDOUT&&j.state==VXL_RUNNING);
 e.kind=VXL_CLOSE; assert(!vxl_job_step(&j,&e)&&j.pinned&&j.closed);
 e.kind=VXL_DONE; e.status=2; e.jobs=1; e.id=2;
 assert(vxl_job_step(&j,&e)==-ESTALE); e.id=1; e.pending_w=1;
 assert(vxl_job_step(&j,&e)==-EAGAIN&&j.pinned); e.pending_w=0; e.idle=1; e.ready=1;
 assert(!vxl_job_step(&j,&e)&&!j.pinned&&j.state==VXL_COMPLETED);
 assert(vxl_job_step(&j,&e)==-EALREADY);
 j.state=VXL_RUNNING; j.pinned=1; e.kind=VXL_DEADLINE;
 assert(!vxl_job_step(&j,&e)&&j.state==VXL_QUARANTINED&&j.pinned);
 e.kind=VXL_START; assert(vxl_job_step(&j,&e)==-EIO);
 assert(vxl_cdma_status(0x1002)==1); assert(vxl_cdma_status(2)==0);
 assert(vxl_cdma_status(0x5002)==-EIO);
 assert(!vxl_cdma_addresses(0x580000000ULL,0x10020000,16));
 assert(vxl_cdma_addresses(1ULL<<40,0,16)==-ERANGE);
 assert(vxl_cdma_addresses(16,0,17)==-EINVAL);
 assert(!vxl_probe_check(1,1,1,1,0x56580204,0x40404,0));
 assert(vxl_probe_check(0,1,1,1,0,0,0)==-ENODEV);
 assert(vxl_probe_check(1,0,1,1,0,0,0)==-ENODEV);
 assert(vxl_probe_check(1,1,0,1,0,0,0)==-ENODEV);
 assert(vxl_probe_check(1,1,1,1,0,0,0)==-ENODEV);
 assert(vxl_probe_check(1,1,1,0,0,0,0)==-EAGAIN);
 assert(vxl_probe_check(1,1,1,1,0x56580204,0x40404,1)==-EBUSY);
 puts("CORE_RANGE_POOL_ABI_JOBS_PROBE_CDMA_PASS");
 printf("ABI sizeof=%zu user_ptr=%zu domain=%zu\n",sizeof(r),offsetof(struct vxl_request,user_ptr),offsetof(struct vxl_request,domain));
 return 0;
}

/* Portable mechanisms shared with the Linux adapter; no MMIO in this header. */
#ifndef VXL_V2_TYPES_H
#define VXL_V2_TYPES_H
#include <uapi/vx_compute_v2.h>
#ifdef __KERNEL__
#include <linux/errno.h>
#else
#include <errno.h>
#endif
static inline int vxl_v2_request_valid(const struct vxl_compute_v2_request *r,unsigned int flags,int compat32)
{
 unsigned int i;
 if(compat32)return -EOPNOTSUPP;
 if(!r || r->major!=2 || r->size!=128 || r->reserved || (r->flags & ~flags))return -EINVAL;
 for(i=0;i<8;i++)if(r->reserved_tail[i])return -EINVAL;
 if(r->length>~(__u64)0-r->offset || r->payload_bytes>~(__u64)0-r->user_ptr)return -EOVERFLOW;
 return 0;
}
static inline int vxl_v2_job_valid(const struct vxl_v2_job *j)
{
 unsigned int i;
 if(!j || j->major!=2 || j->size!=512 || (j->flags & ~VXL_V2_JOB_FAST_RUNTIME) || j->reserved0 || j->reserved1 || j->num_refs>8)return -EINVAL;
 if(!j->cookie || !j->program_handle || !j->program_generation || !j->device_generation || !j->args_handle || !j->args_generation)return -EINVAL;
 if(!j->execution_budget_ns || j->execution_budget_ns>60000000000ULL || !j->args_bytes || j->args_bytes>4096 || (j->args_offset & 63u))return -EINVAL;
 if(j->args_bytes>~(__u64)0-j->args_offset)return -EOVERFLOW;
 for(i=0;i<8;i++)if(j->reserved[i])return -EINVAL;
 for(i=0;i<8;i++){
  const struct vxl_v2_ref *r=&j->refs[i];
  if(i>=j->num_refs){
   if(r->handle || r->object_generation || r->offset || r->length || r->access || r->domain || r->device_generation)return -EINVAL;
   continue;
  }
  if(r->device_generation!=j->device_generation)return -ESTALE;
  if(!r->handle || !r->object_generation || !r->length || !r->access || (r->access & ~3u) || (r->domain!=1 && r->domain!=2) || (r->offset & 63u))return -EINVAL;
  if(r->length>~(__u64)0-r->offset)return -EOVERFLOW;
 }
 return 0;
}
#endif

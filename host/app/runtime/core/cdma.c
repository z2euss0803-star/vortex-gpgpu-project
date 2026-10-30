#include <vxl/core.h>
int vxl_cdma_run(struct vxl_cdma_io *io,vxl_u64 src,vxl_u64 dst,vxl_u32 n,vxl_u64 ms){
 vxl_u64 start,end;vxl_u32 sr;int rc;
 if(!io||!io->read||!io->write||!io->now||!io->pause||!ms||ms>60000)return -EINVAL;
 rc=vxl_cdma_addresses(src,dst,n);if(rc)return rc;
 start=io->now(io->context);if(start>~(vxl_u64)0-ms)return -EOVERFLOW;end=start+ms;
 sr=io->read(io->context,4);io->last_status=sr;
 if(sr&0x4770)return -EIO;
 if(!(sr&2))return -EBUSY;
 io->write(io->context,4,0x3000);sr=io->read(io->context,4);io->last_status=sr;
 if(sr&0x7770)return -EIO;
 io->write(io->context,0x18,(vxl_u32)src);io->write(io->context,0x1c,(vxl_u32)(src>>32));
 io->write(io->context,0x20,(vxl_u32)dst);io->write(io->context,0x24,(vxl_u32)(dst>>32));
 io->write(io->context,0x28,n);
 do {
  sr=io->read(io->context,4);io->last_status=sr;rc=vxl_cdma_status(sr);
  if(rc<0)return rc;
  if(rc>0)return 0;
  io->pause(io->context);
 }while(io->now(io->context)<end);
 return -ETIMEDOUT;
}

#include <vxl/core.h>
static int padded(vxl_u64 n,vxl_u64 *out) {
 if(!n) return -EINVAL;
 if(n>~(vxl_u64)0-63) return -EOVERFLOW;
 *out=(n+63)&~63ULL; return 0;
}
int vxl_pool_reserve(struct vxl_pool *p,vxl_u64 off,vxl_u64 n) {
 unsigned i,empty=VXL_MAX_OBJECTS+1; int rc;
 if(!p || (off&63)||(n&63)) return -EINVAL;
 rc=vxl_range_check(p->capacity,off,n); if(rc) return rc;
 for(i=0;i<VXL_MAX_OBJECTS+1;i++) {
  struct vxl_extent *e=&p->ext[i];
  if(!e->size) {empty=i;continue;}
  if(off<e->offset+e->size && e->offset<off+n) return -EBUSY;
 }
 if(empty==VXL_MAX_OBJECTS+1) return -ENOMEM;
 p->ext[empty].offset=off;p->ext[empty].size=n;return 0;
}
int vxl_pool_alloc(struct vxl_pool *p,vxl_u64 n,vxl_u64 align,vxl_u64 *out) {
 unsigned i; vxl_u64 off=0,next; int rc;
 if(!p||!out||align<64||(align&(align-1))) return -EINVAL;
 rc=padded(n,&n);if(rc) return rc;
 for(;;) {
  if(off>~(vxl_u64)0-(align-1)) return -EOVERFLOW;
  off=(off+align-1)&~(align-1);
  if(vxl_range_check(p->capacity,off,n)) return -ENOMEM;
  next=off;
  for(i=0;i<VXL_MAX_OBJECTS+1;i++) {
   struct vxl_extent *e=&p->ext[i];
   if(e->size&&off<e->offset+e->size&&e->offset<off+n && e->offset+e->size>next) next=e->offset+e->size;
  }
  if(next==off) {rc=vxl_pool_reserve(p,off,n);if(!rc)*out=off;return rc;}
  off=next;
 }
}
int vxl_pool_free(struct vxl_pool *p,vxl_u64 off,vxl_u64 n) {
 unsigned i;if(!p)return -EINVAL;
 for(i=0;i<VXL_MAX_OBJECTS+1;i++) if(p->ext[i].size==n&&n&&p->ext[i].offset==off) {p->ext[i].size=0;return 0;}
 return -EBADF;
}

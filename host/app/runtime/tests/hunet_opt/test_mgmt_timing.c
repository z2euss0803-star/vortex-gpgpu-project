#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vxl/mgmt_timing.h>
static struct vxl_mgmt_row rows[512];
static char csv[1024*1024];static size_t csv_used;
static int sink(void *p,const char *text,size_t n){(void)p;assert(csv_used+n<sizeof(csv));memcpy(csv+csv_used,text,n);csv_used+=n;csv[csv_used]=0;return 0;}
int main(void){
 struct vxl_mgmt_trace t={0};struct vxl_mgmt_row *r;unsigned i;
 struct vxl_mgmt_key a={1,2,3,4},b={5,6,7,8};
 t.rows=rows;t.capacity=512;
 assert(vxl_mgmt_new_epoch(&t)==0);
 assert(vxl_mgmt_begin(NULL,&a,0)==NULL);assert(vxl_mgmt_add(NULL,VXL_MGMT_VALIDATE,0,10,0)==0);
 r=vxl_mgmt_begin(&t,&a,100);assert(r && (r->flags&VXL_MGMT_INCOMPLETE));
 assert(vxl_mgmt_add(r,VXL_MGMT_VALIDATE,101,110,0)==0);
 assert(vxl_mgmt_add(r,VXL_MGMT_INPUT_READ,112,120,4096)==0);
 assert(vxl_mgmt_nested_add(r,VXL_MGMT_CDMA_TOTAL,112,119,4096)==0);
 vxl_mgmt_finish(&t,130,-EIO);assert(r->result==-EIO && r->total_ns==30 && !r->flags);
 assert(r->ns[VXL_MGMT_VALIDATE]==9 && r->ns[VXL_MGMT_INPUT_READ]==8);
 assert(r->nested_ns[VXL_MGMT_CDMA_TOTAL]==7);
 assert(vxl_mgmt_emit(&t,sink,NULL)==0);assert(strstr(csv,"#VXL_MGMT_TIMING_V1") && strstr(csv,"input_read_ns"));
 assert(strstr(csv,"nested_cdma_total_ns"));
 /* A failure cannot be discarded by opening a new epoch. */
 t.exported_used=t.used;assert(vxl_mgmt_new_epoch(&t)==-EIO);
 r=vxl_mgmt_begin(&t,&a,200);assert(vxl_mgmt_begin(&t,&b,201)==NULL);
 assert((t.flags&(VXL_MGMT_BUSY|VXL_MGMT_STALE))==(VXL_MGMT_BUSY|VXL_MGMT_STALE));
 vxl_mgmt_finish(&t,210,0);
 r=vxl_mgmt_begin(&t,&a,300);assert(vxl_mgmt_add(r,VXL_MGMT_GUARD,310,309,0)==-EINVAL);
 assert(r->flags&VXL_MGMT_BACKWARDS);vxl_mgmt_finish(&t,320,0);
 r=vxl_mgmt_begin(&t,&a,400);assert(vxl_mgmt_add(r,VXL_MGMT_GUARD,401,410,0)==0);
 assert(vxl_mgmt_add(r,VXL_MGMT_STATE,409,420,0)==-EINVAL);assert(r->flags&VXL_MGMT_BUSY);vxl_mgmt_finish(&t,430,0);
 r=vxl_mgmt_begin(&t,&a,500);r->ns[VXL_MGMT_GUARD]=~(vxl_u64)0-1;
 assert(vxl_mgmt_add(r,VXL_MGMT_GUARD,501,510,0)==-EOVERFLOW);assert(r->flags&VXL_MGMT_OVERFLOW);vxl_mgmt_finish(&t,520,0);
 for(i=t.used;i<512;i++){r=vxl_mgmt_begin(&t,&a,1000+i*10);assert(r);vxl_mgmt_finish(&t,1001+i*10,0);}
 assert(t.used==512 && vxl_mgmt_begin(&t,&a,9999)==NULL && t.dropped==1 && (t.flags&VXL_MGMT_FULL));
 /* An unfinished record is not silently complete, even when serialized. */
 memset(&t,0,sizeof(t));memset(rows,0,sizeof(rows));t.rows=rows;t.capacity=512;
 r=vxl_mgmt_begin(&t,&a,1);assert(r->flags&VXL_MGMT_INCOMPLETE);
 assert(vxl_mgmt_emit(&t,sink,NULL)==-EBUSY);vxl_mgmt_finish(&t,0,0);assert(r->flags&VXL_MGMT_BACKWARDS);
 memset(&t,0,sizeof(t));t.rows=rows;t.capacity=512;
 for(i=0;i<32;i++){
  a.generation=i+1;r=vxl_mgmt_begin(&t,&a,100);assert(r);vxl_mgmt_finish(&t,200,0);
  assert(vxl_mgmt_new_epoch(&t)==-EBUSY); /* snapshot must precede recycling */
  t.exported_used=t.used;assert(vxl_mgmt_new_epoch(&t)==0);assert(!t.used&&!t.flags&&!t.dropped);
 }
 puts("MGMT_CAPACITY_OWNER_OVERFLOW_CLOSURE_AND_CSV_PASS_HOST_ONLY");return 0;
}

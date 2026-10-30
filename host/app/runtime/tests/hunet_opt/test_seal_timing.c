#include "../hunet/memory_fixture.h"
int main(void){
 struct fixture f;struct vxl_mgmt_row row;struct vxl_mgmt_trace t={0};struct vxl_mgmt_key key={123,1,0,VXL_HUNET_SEAL};
 init_fixture(&f);t.rows=&row;t.capacity=1;f.c.mgmt_row=vxl_mgmt_begin(&t,&key,100000000);
 seal_fixture(&f);vxl_mgmt_finish(&t,100000000,0);assert(!row.flags);
 assert(row.bytes[VXL_MGMT_SEAL_CODE_READ]==f.r.program[0].wire_bytes);
 assert(row.bytes[VXL_MGMT_SEAL_CODE_HASH]==f.r.program[0].wire_bytes);
 assert(row.bytes[VXL_MGMT_SEAL_WEIGHTS_READ]==23685056);
 assert(row.bytes[VXL_MGMT_SEAL_WEIGHTS_HASH]==23685056);
 assert(row.calls[VXL_MGMT_SEAL_WEIGHTS_READ]==5783);
 f.c.mgmt_row=NULL;destroy_fixture(&f);puts("SEAL_REAL_READ_HASH_BYTES_AND_CHUNK_COUNTS_PASS_HOST_ONLY");return 0;
}

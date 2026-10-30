#ifndef VXL_MOCK_H
#define VXL_MOCK_H
#include <vxl/engine.h>
enum vxl_mock_fault { MOCK_OK, MOCK_NO_DONE, MOCK_ERROR, MOCK_PENDING, MOCK_STALE_JOBS, MOCK_BAD_ACK, MOCK_COPY_ERROR, MOCK_COPY_TIMEOUT, MOCK_READ_USER, MOCK_WRITE_USER, MOCK_BAD_ID, MOCK_NOT_READY };
struct vxl_mock {struct vxl_engine engine; vxl_u64 clock,finish; int fault,fail_alloc,alloc_calls; unsigned allocations,transfers,starts; vxl_u32 jobs;};
void vxl_mock_init(struct vxl_mock *);
void vxl_mock_advance(struct vxl_mock *,vxl_u64 ms);
#endif

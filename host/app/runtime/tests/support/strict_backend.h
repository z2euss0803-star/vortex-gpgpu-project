#ifndef VXL_TEST_STRICT_BACKEND_H
#define VXL_TEST_STRICT_BACKEND_H
#include <vxl/engine.h>
struct strict_backend {
 struct vxl_engine engine;
 vxl_u64 clock;
 unsigned starts,transfers,allocations;
 int transfer_error,completion_error,completed;
 vxl_u32 jobs;
};
void strict_init(struct strict_backend *);
void strict_dispose(struct strict_backend *);
void strict_complete(struct strict_backend *);
#endif

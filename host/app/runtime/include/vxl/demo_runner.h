#ifndef VXL_DEMO_RUNNER_H
#define VXL_DEMO_RUNNER_H
#include <vxl/demo_result.h>
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
struct demo_io_ops {
 ssize_t (*write)(void *,int,const void *,size_t);
 int (*sync)(void *,int);
 int (*publish)(void *,int,const char *,const char *);
 void *context;
};
struct demo_store { int dir_fd;struct demo_io_ops io;uint64_t sequence; };
struct demo_session;
struct demo_dispatch;
struct demo_gpu_ops {
 int (*open_owner)(void *);
 int (*close_owner)(void *,int);
 int (*start)(void *,int,const struct demo_run *,uint64_t *);
 /* 0 still active, 1 completed, negative unknown/error. */
 int (*poll)(void *,int,uint64_t);
 /* 1 trusted drained; 0 or negative must pin the session. */
 int (*quiescent)(void *,int);
 int (*collect)(void *,int,uint64_t,struct demo_result *);
 void *context;
};
struct demo_session {
 int owner_fd,quarantined;
 uint32_t generation;
 uint64_t job;
 enum demo_state state;
 struct demo_run request;
 struct demo_result result;
 struct demo_gpu_ops gpu;
 struct demo_store *store;
 struct demo_dispatch *dispatcher;
 /* Process-local current system identity; no ioctl or wire ABI change. */
 int identity_bound;
 char boot_id[129],build_id[129];
 /* Host-only provider lifetime; no public/wire ABI change. */
 int (*release_provider_close)(struct demo_session *);
};
int demo_store_open(struct demo_store *,const char *,const struct demo_io_ops *);
void demo_store_close(struct demo_store *);
int demo_stage(struct demo_store *,const struct demo_asset *,int);
int demo_publish(struct demo_store *,const struct demo_result *);
int demo_event(struct demo_store *,const char *,const char *);
int demo_record_request(struct demo_store *,const struct demo_run *);
int demo_restore_pending(struct demo_store *,struct demo_run *);
int demo_store_run_asset(struct demo_store *,const char *,const char *,const void *,size_t);
int demo_store_read_result(struct demo_store *,const char *,char *,size_t,size_t *);
int demo_session_init(struct demo_session *,struct demo_store *,uint32_t,const struct demo_gpu_ops *);
int demo_session_bind_identity(struct demo_session *,const char *,const char *);
int demo_owner_acquire(struct demo_session *);
int demo_owner_release(struct demo_session *);
int demo_enqueue(struct demo_session *,const struct demo_run *);
int demo_start(struct demo_session *,const struct demo_run *);
int demo_status(struct demo_session *,const char *,struct demo_result *);
void demo_sha_hex(const void *,size_t,char [65]);
#ifdef __cplusplus
}
#endif
#endif

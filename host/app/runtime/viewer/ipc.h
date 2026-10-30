#ifndef DEMO_VIEWER_IPC_H
#define DEMO_VIEWER_IPC_H
#include <vxl/demo_frame.h>
/* Reference only; the caller cannot select a filesystem path. */
int dp_load_reference(struct demo_store *,const char *request,size_t,struct demo_frame *);
/* Bounded local-UID request; one callback, never a shell or arbitrary path. */
int dp_serve_connection(int,uid_t,int (*)(void *,const char *,size_t),void *);
#endif

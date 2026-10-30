#ifndef VXL_DEMO_RELEASE_H
#define VXL_DEMO_RELEASE_H
#include <vxl/demo_dispatch.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Final source-bound T19 provider installs both adapters and verified assets.
 * An unbound executable must fail closed, never substitute a software model. */
int demo_release_install(struct demo_session *,struct demo_store *,const char *);
/* Close while the caller's session still exists. A fault retains its provider
 * and pinned ownership instead of freeing an unproven active runtime. */
static inline int demo_release_uninstall(struct demo_session *s){return !s?-EINVAL:s->release_provider_close?s->release_provider_close(s):0;}
#ifdef __cplusplus
}
#endif
#endif

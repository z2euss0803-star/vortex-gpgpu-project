#ifndef VXL_MOSAIC_H
#define VXL_MOSAIC_H
#include <vxl/mosaic_wire.h>
#include <vxl/mosaic_layout.h>
/* Only trusted catalog/state code constructs expected. Validation is pure:
 * successful validation does not advance a stage or claim GPU completion.
 * T10 adds ownership/quarantine/completion transitions. */
struct mosaic_host_state;
struct mosaic_ctx {struct mosaic_job_v1 expected;mosaic_u32 total_tiles;const struct mosaic_layout *layout;struct mosaic_host_state *host;};
#ifdef __cplusplus
extern "C" {
#endif
mosaic_u32 mosaic_job_crc(const struct mosaic_job_v1 *);
int mosaic_validate_job(struct mosaic_ctx *,const struct mosaic_job_v1 *);
#ifdef __cplusplus
}
#endif
#endif

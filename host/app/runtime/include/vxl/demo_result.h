#ifndef VXL_DEMO_RESULT_H
#define VXL_DEMO_RESULT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define DEMO_MAX_JSON (256u * 1024u)
#define DEMO_MAX_ASSET (64u * 1024u * 1024u)
enum demo_app { DEMO_MOSAIC=1, DEMO_HUNET=2 };
enum demo_state { DEMO_STAGED=1, DEMO_LOADED, DEMO_RUNNING, DEMO_COMPUTE_DONE,
 DEMO_RESULT_VALIDATED, DEMO_FAILED, DEMO_QUARANTINED };
struct demo_run {
 enum demo_app app;
 enum demo_state state;
 char run_id[33];
 uint32_t generation;
 int diagnostics, cycle_valid, timing_valid;
 uint64_t raw_cycles;
 /* Exact validated JSON retains all asset and source identities; no double conversion. */
 size_t json_bytes;
 char json[DEMO_MAX_JSON+1u];
};
struct demo_result { struct demo_run run; char source_result_sha[65]; };
struct demo_asset {
 char name[241], sha256[65]; uint64_t bytes;
 uint32_t shape[4], rank, element_bytes;
};
int demo_parse_run(const char *,size_t,struct demo_run *);
int demo_parse_asset(const char *,size_t,struct demo_asset *);
int demo_write_result(const struct demo_result *,char *,size_t,size_t *);
#ifdef __cplusplus
}
#endif
#endif

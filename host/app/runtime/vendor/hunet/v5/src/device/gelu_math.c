/* Analytic erf GELU; legacy musl FP32 libm replaces double-heavy expf only
 * inside this private implementation. No global math or fast-math override. */
#include "../../vendor/libm.h"
#ifdef __cplusplus
extern "C" {
#endif
#define expf hunet_expf
#include "../../vendor/expf_float.c"
#define erff hunet_erff
#define erfcf hunet_erfcf
#include "../../vendor/erff.c"
#ifdef __cplusplus
}
#endif

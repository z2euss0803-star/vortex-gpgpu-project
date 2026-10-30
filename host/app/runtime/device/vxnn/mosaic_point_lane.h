#ifndef MOSAIC_POINT_LANE_H
#define MOSAIC_POINT_LANE_H
#include <stdint.h>
// Trusted item helper. The entry owns global item/core assignment and validates
// relocated spans/lifecycle. full_spatial is the complete channel size, never
// this lane's slice length; first/count describe one <=256-element item.
struct mosaic_point_lane_args {
 uint32_t opcode,lane,full_spatial,full_elements,first,count;
 float scale;bool save_input;
 const float *input,*skip,*stats,*gamma,*beta;
 float *output,*saved_input;
 int16_t *quant;
};
struct mosaic_point_lane_prepared {
 float values[64];int16_t quantized[64];
 float *output,*saved_input;int16_t *quant;
 uint32_t first,count,opcode;
 bool save_input,ready;
};
// prepare writes only lane-private scratch, never any business destination.
// Even an empty lane participates in the caller's agreement before commit.
int mosaic_point_lane_prepare(const mosaic_point_lane_args *,mosaic_point_lane_prepared *);
// Caller supplies the common four-lane agreement result. Nonzero -> no writes.
// Commit has no arithmetic that can fail. Inputs/descriptors must remain frozen
// until all lanes finish commit; callers synchronize before consuming outputs.
enum mosaic_point_lane_commit_phase : uint32_t {MOSAIC_POINT_COMMIT_QUANT=0,MOSAIC_POINT_COMMIT_FLOAT=1};
// All lanes complete QUANT, then caller syncs, then FLOAT. This preserves
// whole-item quant-before-saved ordering; op4/5/6 require only FLOAT.
void mosaic_point_lane_commit(const mosaic_point_lane_prepared *,int agreed_rc,mosaic_point_lane_commit_phase);
#endif

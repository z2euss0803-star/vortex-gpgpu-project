#pragma once
#include <vxnn/types.hpp>
namespace vxnn {
struct MosaicPhase {uint32_t width=0,height=0,index=0,layer=0,opcode=0,items=0;};
struct MosaicBatch {uint32_t first=0,count=0;std::array<uint32_t,4>core_items{};};
class MosaicCoverage {
 MosaicPhase phase_;uint32_t budget_=0;uint64_t jobs_=0;
 MosaicCoverage(MosaicPhase p,uint32_t b,uint64_t n):phase_(p),budget_(b),jobs_(n){}
 friend Result<MosaicCoverage>bounded_tiles(const MosaicPhase&,uint32_t);
public:
 const MosaicPhase&phase()const{return phase_;}uint32_t budget()const{return budget_;}uint64_t jobs()const{return jobs_;}
 Result<MosaicBatch>at(uint64_t)const;
};
// Frozen model geometry lives in user state. No old job/address descriptor,
// device load, GPU operation or timing is emitted by this metadata planner.
Result<MosaicPhase>describe_mosaic_phase(uint32_t width,uint32_t height,uint32_t phase);
Result<MosaicCoverage>bounded_tiles(const MosaicPhase&,uint32_t item_budget);
}

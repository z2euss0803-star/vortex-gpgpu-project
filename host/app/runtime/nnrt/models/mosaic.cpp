#include <vxnn/mosaic_coverage.hpp>
#include <cerrno>
#include <limits>
namespace vxnn {
Result<MosaicPhase>describe_mosaic_phase(uint32_t width,uint32_t height,uint32_t phase){
 if(width<8||width>1280||height<8||height>960||width%4||height%4)return Result<MosaicPhase>::failure({EINVAL,"MOSAIC_COVERAGE","frozen geometry range/multiple4"});
 if(phase>=77)return Result<MosaicPhase>::failure({ERANGE,"MOSAIC_COVERAGE","77 phases only"});
 // Model topology is user-state metadata; no layer table goes into the driver.
 static const uint32_t channels[16]={32,64,128,128,128,128,128,128,128,128,128,128,128,64,32,3};
 uint32_t layer=phase<75?phase/5:15,part=phase<75?phase%5:phase-75;
 uint64_t w=width,h=height,ci=3;
 for(uint32_t i=0;i<layer;i++){uint32_t stride=i==1||i==2?2:1,up=i==13||i==14?2:1;w=(w*up+stride-1)/stride;h=(h*up+stride-1)/stride;ci=channels[i];}
 uint32_t stride=layer==1||layer==2?2:1,up=layer==13||layer==14?2:1;
 uint64_t ow=(w*up+stride-1)/stride,oh=(h*up+stride-1)/stride;
 uint32_t opcode=part+1;if(part==4)opcode=layer>=4&&layer<=12&&!(layer&1)?6:5;
 uint64_t items=opcode==1?(ci*w*h+255)/256:opcode==2?((channels[layer]+7)/8)*((ow*oh+7)/8):opcode==3?channels[layer]:(channels[layer]*ow*oh+255)/256;
 if(!items||items>std::numeric_limits<uint32_t>::max())return Result<MosaicPhase>::failure({EOVERFLOW,"MOSAIC_COVERAGE","phase item extent"});
 return Result<MosaicPhase>::success({width,height,phase,layer,opcode,uint32_t(items)});
}
Result<MosaicCoverage>bounded_tiles(const MosaicPhase&p,uint32_t budget){
 if(!budget||budget>4096)return Result<MosaicCoverage>::failure({ERANGE,"MOSAIC_COVERAGE","original1..4096 item limit"});
 auto actual=describe_mosaic_phase(p.width,p.height,p.index);if(!actual)return Result<MosaicCoverage>::failure(*actual.error);const auto&a=*actual.value;
 if(p.layer!=a.layer||p.opcode!=a.opcode||p.items!=a.items)return Result<MosaicCoverage>::failure({EINVAL,"MOSAIC_COVERAGE","phase metadata mismatch"});
 return Result<MosaicCoverage>::success(MosaicCoverage(p,budget,(uint64_t(p.items)+budget-1)/budget));
}
Result<MosaicBatch>MosaicCoverage::at(uint64_t index)const{
 if(index>=jobs_)return Result<MosaicBatch>::failure({ERANGE,"MOSAIC_COVERAGE","batch index"});
 const uint64_t begin=index*budget_; // index bounded before multiplication
 if(begin>=phase_.items)return Result<MosaicBatch>::failure({EOVERFLOW,"MOSAIC_COVERAGE","batch extent"});
 MosaicBatch out;out.first=uint32_t(begin);out.count=phase_.items-out.first;if(out.count>budget_)out.count=budget_;
 for(uint32_t core=0;core<4;core++){uint32_t delta=(core+4-out.first%4)%4;out.core_items[core]=delta<out.count?1+(out.count-1-delta)/4:0;}
 return Result<MosaicBatch>::success(out);
}
}

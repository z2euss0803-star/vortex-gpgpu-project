#pragma once
#include <vxnn/package.hpp>
#include <vxnn/mosaic_ro64_program.hpp>
#include <vxnn/mosaic_fused_program.hpp>
#include <vxnn/mosaic_coop_program.hpp>
#include <vxnn/mosaic_reuse_program.hpp>
#include <vxnn/mosaic_reuse_v8_program.hpp>
namespace vxnn {
enum class MosaicProgramVariant {UNKNOWN,RO64,FUSED,COOPERATIVE,REUSE};
inline MosaicProgramVariant mosaic_program_variant(const Digest&schema){
 const auto value=digest_hex(schema);
 if(value=="c6564333cf23245cedd82391cdf5054fdcf3fa677388e7ba6d05ca3d795df43d")return MosaicProgramVariant::RO64;
 if(value==mosaic_fused_schema_sha256)return MosaicProgramVariant::FUSED;
 if(value==mosaic_coop_schema_sha256)return MosaicProgramVariant::COOPERATIVE;
 if(value==mosaic_reuse_v8_schema_sha256)return MosaicProgramVariant::REUSE;
 return MosaicProgramVariant::UNKNOWN;
}
inline const char*mosaic_published_code(const Digest&schema){
 switch(mosaic_program_variant(schema)){
 case MosaicProgramVariant::RO64:return mosaic_ro64_code_sha256;
 case MosaicProgramVariant::FUSED:return mosaic_fused_code_sha256;
 case MosaicProgramVariant::COOPERATIVE:return mosaic_coop_code_sha256;
 case MosaicProgramVariant::REUSE:return mosaic_reuse_v8_code_sha256;
 default:return nullptr;
 }
}
inline bool mosaic_program_code_matches(const Digest&schema,const Digest&code){
 const char*expected=mosaic_published_code(schema);
 return expected&&std::string(expected).size()==64&&digest_hex(code)==expected;
}
inline bool mosaic_parameter_abi_matches(MosaicProgramVariant variant,uint32_t abi,uint32_t op){
 switch(variant){
 case MosaicProgramVariant::RO64:return abi==1&&op<=6;
 case MosaicProgramVariant::FUSED:return abi==(op>=7?2u:1u);
 case MosaicProgramVariant::COOPERATIVE:return abi==3;
 case MosaicProgramVariant::REUSE:return abi==4;
 default:return false;
 }
}
}

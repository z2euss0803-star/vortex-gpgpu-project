#include <vxnn/mosaic_program_family.hpp>
#include <vxnn/mosaic_reuse_v8_program.hpp>
#include <cstdio>
#include <cstdlib>
using namespace vxnn;
static Digest digest(const char* value) {
 Digest out{};
 for (unsigned i=0;i<32;i++) out[i]=uint8_t(std::strtoul(std::string(value+i*2,2).c_str(),nullptr,16));
 return out;
}
int main() {
 const auto schema=digest("9a03920a64917e13254a20e235bfb01bee937a0d199594def5f578daa6d21dd4");
 const auto next=digest("618632402b615385dd165f7fa25a4526843cb7e975517a3c7fb16d27435fa34c");
 const auto old=digest("ff0446e45cb013772c772442cd22f3bb98ff216ee217c8c5606acb1fb9593a59");
 if(mosaic_program_variant(schema)!=MosaicProgramVariant::REUSE) return 19;
 if(!mosaic_program_code_matches(schema,next)) { std::puts("NEW_V8_CODE_REJECTED_BY_CPP_PIN"); return 20; }
 if(mosaic_program_code_matches(schema,old)) { std::puts("OLD_V7_CODE_ACCEPTED_BY_R13_REUSE_GATE"); return 21; }
 if(mosaic_program_code_matches(schema,digest("0000000000000000000000000000000000000000000000000000000000000000"))) return 22;
 const auto ro64=digest("c6564333cf23245cedd82391cdf5054fdcf3fa677388e7ba6d05ca3d795df43d");
 if(!mosaic_program_code_matches(ro64,digest("8157dfd8a05d2ba6ec0e3ef9d6dd8bd545078afc8ce18e46856c9be2babe82d4"))) return 23;
 if(!mosaic_parameter_abi_matches(MosaicProgramVariant::REUSE,4,8) || mosaic_parameter_abi_matches(MosaicProgramVariant::REUSE,3,8)) return 24;
 std::puts("CPP_R13_NEW_CODE_ONLY_AND_LEGACY_RO64_PIN_PASS");
 return 0;
}

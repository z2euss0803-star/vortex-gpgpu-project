#include <vxunified/types.hpp>
#include <cassert>
#include <iostream>
using namespace vxunified;
static Digest literal(const char*s){Digest d{};for(unsigned i=0;i<32;i++){auto n=[](char c){return unsigned(c<='9'?c-'0':c-'a'+10);};d[i]=(n(s[i*2])<<4)|n(s[i*2+1]);}return d;}
int main(){
 BackendIdentity b;b.hardware_contract.fill(1);b.bit.fill(2);b.xsa.fill(3);b.compatible.fill(4);b.device_generation=~uint64_t(0);b.provider_flags=1;
 ProgramKey nn;nn.namespace_id=1;nn.logical_profile=5;nn.logical_program=9;nn.hardware_contract=b.hardware_contract;nn.code.fill(8);nn.schema.fill(9);
 auto saved=b;assert(admit_program(b,nn));
 ProgramKey p8=nn;p8.namespace_id=2;p8.hardware_contract.fill(2);assert(!admit_program(b,p8));
 p8.hardware_contract=b.hardware_contract;assert(!admit_program(b,p8));
 assert(b.device_generation==saved.device_generation && !b.board_identity_verified);
 auto caps=query_caps(b,DeviceCaps{});assert(caps.device_generation==saved.device_generation && caps.contexts==1 && caps.identity_source==1 && !b.board_identity_verified);
 auto from_caps=b;from_caps.provider_flags=caps.feature_mask;assert(admit_program(from_caps,nn));assert(!admit_program(from_caps,p8));
 for(unsigned n: {0u,4u,~0u}){auto p=nn;p.namespace_id=n;assert(!admit_program(b,p));}
 auto p=nn;p.code={};assert(!admit_program(b,p));p=nn;p.schema={};assert(!admit_program(b,p));
 auto stale=b;stale.device_generation=0;assert(!admit_program(stale,nn));
 auto both=b;both.provider_flags=3;assert(!admit_program(both,nn));
 auto graphics=b;graphics.provider_flags=2;graphics.hardware_contract.fill(2);p8.hardware_contract=graphics.hardware_contract;assert(admit_program(graphics,p8));
 auto image=nn;image.namespace_id=3;image.logical_profile=256;image.logical_program=512;assert(admit_program(b,image));
 auto joint=b;joint.provider_flags=4;joint.hardware_contract=literal("1bf11d26789e7c602f9c05a5ecc97e2605ac7807deade8f4bfc549487f45ec5d");joint.bit=literal("94ae9d3e6eb73ba55a8eec8c802e3818cd6b3a9be7e60542da5ae9459e3579e1");joint.xsa=literal("02e68160e21b01621fde1eac6e472810e0fe50cd1842acb3ab63469314355e94");joint.compatible=literal("f670c653a37361f05f57c14eecd53dea1b87556489a2c1314efd208d715826dc");
 auto j=nn;j.hardware_contract=joint.hardware_contract;j.logical_profile=1;j.logical_program=65552;assert(admit_program(joint,j));j.logical_program=65568;assert(admit_program(joint,j));j.logical_program=65537;assert(!admit_program(joint,j));
 j=p8;j.hardware_contract=joint.hardware_contract;assert(admit_program(joint,j));j.namespace_id=3;j.logical_profile=256;j.logical_program=512;assert(admit_program(joint,j));
 auto wrong=joint;wrong.compatible[0]^=1;assert(!admit_program(wrong,j));wrong=joint;wrong.bit[0]^=1;assert(!admit_program(wrong,j));
 DisplayReceipt receipt;assert(!receipt.presented && !receipt.old_retired && !receipt.presented_ns);
 std::cout<<"UNIFIED_FULL_IDENTITY_ADMISSION_PASS_NOT_BOARD\n";
}

#include <vxunified/catalog.hpp>
#include <vxl/v2/program.h>
#include <vxl/hunet.h>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <atomic>
using namespace vxunified;
namespace {
void need(bool b,const char*s){if(!b){std::fprintf(stderr,"U12_CATALOG_ASSERT %s\n",s);std::exit(73);}}
Digest hex(const char*s){Digest d{};for(size_t i=0;i<32;i++){unsigned x=0;need(std::sscanf(s+2*i,"%2x",&x)==1,"literal digest");d[i]=x;}return d;}
Digest hash(const std::vector<uint8_t>&v){Digest d{};vxl_hunet_hash(v.data(),v.size(),d.data());return d;}
std::vector<uint8_t>read(const char*p){std::ifstream f(p,std::ios::binary|std::ios::ate);need(bool(f),"catalog fixture file");auto n=f.tellg();need(n>=64&&n<=65536,"bounded fixture");std::vector<uint8_t>v(size_t(n),0);f.seekg(0);f.read(reinterpret_cast<char*>(v.data()),n);need(bool(f),"complete fixture");return v;}
BackendIdentity hw(){BackendIdentity b;b.hardware_contract.fill(1);b.bit.fill(2);b.xsa.fill(3);b.compatible.fill(4);b.provider_flags=1;b.device_generation=7;return b;}
ProgramKey key(){ProgramKey k;k.namespace_id=1;k.logical_profile=1;k.logical_program=65552;k.hardware_contract.fill(1);k.code=hex("b3d9bf022a135fa4b528f68fc49e653ab9f3c649281623d39a5f3d57756b594c");k.schema=hex("79319f1611265bc2ad131b91642de7ddbe0c30d4b3621ce3a8060e90e1114c89");return k;}
}
int main(int n,char**v){
 need(n==3,"one frozen and one ambiguous fixture");auto raw=read(v[1]),ambiguous=read(v[2]);const auto expected=hex("99f5a31edfa7a80b003f27e7b7b28673b4952a4f46a94ac7fe1cd3f35a2950f0");need(hash(raw)==expected,"independent frozen catalog SHA");auto bound=InstalledCatalog::decode({raw.data(),raw.size()},hw(),expected);
 if(!bound){std::fprintf(stderr,"CATALOG_ERROR %s\n",bound.error->detail.c_str());}
 need(bool(bound),"valid installed catalog must bind");auto c=*bound.value;need(c->size()==1&&c->sha()==expected&&c->identity().device_generation==7&&!c->identity().board_identity_verified,"cached declaration with no board promotion");auto k=key();auto program=c->resolve(k);need(bool(program),"exact new controlled HUNet key");Digest entry{};std::memcpy(entry.data(),raw.data()+320,32);need(program.value->entry_id==entry&&entry!=k.code&&program.value->catalog_sha==expected&&program.value->device_generation==7&&program.value->code_bytes==65536&&program.value->args_bytes==128,"entryID is exact encoded entry, never CODE SHA");
 raw[128]^=1;need(c->resolve(k).value->entry_id==entry,"decoded bytes owned immutably");need(!InstalledCatalog::decode({raw.data(),raw.size()},hw(),expected),"tamper rejected against trusted expected SHA");
 auto wrong=hw();wrong.bit[0]^=1;auto original=read(v[1]);auto mismatch=InstalledCatalog::decode({original.data(),original.size()},wrong,expected);need(!mismatch&&mismatch.error->code==EXDEV,"different bit cannot bind catalog");wrong=hw();wrong.provider_flags=2;need(!InstalledCatalog::decode({original.data(),original.size()},wrong,expected),"P8 cannot bind S8 catalog");wrong=hw();wrong.device_generation=0;need(!InstalledCatalog::decode({original.data(),original.size()},wrong,expected),"unknown epoch");Digest zero{};need(!InstalledCatalog::decode({original.data(),original.size()},hw(),zero),"no implicit trust anchor");need(!InstalledCatalog::decode({nullptr,64},hw(),expected),"null storage");
 auto bad=k;bad.code[0]^=1;need(!c->resolve(bad),"CODE mismatch");bad=k;bad.schema[0]^=1;need(!c->resolve(bad),"schema mismatch");bad=k;bad.logical_program=9;need(!c->resolve(bad),"no legacy9 fallback");bad=k;bad.namespace_id=2;need(!c->resolve(bad),"namespace isolation");bad=k;bad.hardware_contract[0]^=1;need(!c->resolve(bad),"hardware contract mismatch");
 vxl_v2_provider provider{};provider.flags=1;std::memset(provider.hardware_contract,1,32);std::memset(provider.bit,2,32);std::memset(provider.xsa,3,32);vxl_v2_catalog checked{};need(!vxl_v2_catalog_open(&checked,ambiguous.data(),ambiguous.size(),&provider)&&checked.count==2,"duplicate-key fixture is valid in original C catalog grammar");auto duplicate=InstalledCatalog::decode({ambiguous.data(),ambiguous.size()},hw(),hash(ambiguous));need(!duplicate&&duplicate.error->code==EEXIST,"ambiguous ProgramKey cannot pick first entry");
 auto malformed=original;std::memset(malformed.data()+12,0,4);need(!InstalledCatalog::decode({malformed.data(),malformed.size()},hw(),hash(malformed)),"trusted hash cannot bypass C grammar validation");
 std::atomic<unsigned>bad_reads{0};std::vector<std::thread>threads;for(unsigned i=0;i<8;i++)threads.emplace_back([&]{for(unsigned j=0;j<2000;j++){auto found=c->resolve(k);if(!found||found.value->entry_id!=entry)++bad_reads;}});for(auto&t:threads)t.join();need(!bad_reads,"concurrent immutable catalog reads");
 puts("U12_INSTALLED_CATALOG_IDENTITY_AMBIGUITY_AND_CONCURRENT_RESOLUTION_PASS_NO_IO gpu_raw_cycles=null");
}

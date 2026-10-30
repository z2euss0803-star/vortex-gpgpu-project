#include <vxunified/catalog.hpp>
#include <vxl/v2/program.h>
#include <vxl/hunet.h>
#include <cstring>
#include <new>
namespace vxunified {
namespace {
bool same_key(const ProgramKey&a,const ProgramKey&b){
 return a.namespace_id==b.namespace_id&&a.logical_profile==b.logical_profile&&
 a.logical_program==b.logical_program&&a.code==b.code&&a.schema==b.schema&&
 a.hardware_contract==b.hardware_contract;
}
}
struct InstalledCatalog::Impl {
 BackendIdentity identity;Digest sha{};std::vector<uint8_t>raw;
 std::vector<CatalogBinding>bindings;
};
InstalledCatalog::InstalledCatalog(std::unique_ptr<Impl>p):impl_(std::move(p)){}
InstalledCatalog::~InstalledCatalog()=default;
Result<std::shared_ptr<const InstalledCatalog>>InstalledCatalog::decode(vxnn::Bytes bytes,const BackendIdentity&hw,const Digest&expected){
 using R=Result<std::shared_ptr<const InstalledCatalog>>;
 if(!bytes.data||bytes.size<64||bytes.size>65536||!nonzero_digest(expected))return R::failure({EINVAL,"CATALOG_BIND","bounded catalog and trusted SHA required"});
 if(!hw.device_generation||!nonzero_digest(hw.hardware_contract)||!nonzero_digest(hw.bit)||!nonzero_digest(hw.xsa)||!nonzero_digest(hw.compatible)||(hw.provider_flags!=1&&hw.provider_flags!=2&&hw.provider_flags!=4))return R::failure({EINVAL,"CATALOG_BIND","complete provider identity required"});
 try{
  auto p=std::make_unique<Impl>();p->identity=hw;
  p->raw.assign(bytes.data,bytes.data+bytes.size);
  vxl_hunet_hash(p->raw.data(),p->raw.size(),p->sha.data());
  if(p->sha!=expected)return R::failure({EBADMSG,"CATALOG_BIND","installed catalog SHA mismatch"});
  vxl_v2_provider provider{};provider.flags=hw.provider_flags;
  std::memcpy(provider.hardware_contract,hw.hardware_contract.data(),32);
  std::memcpy(provider.bit,hw.bit.data(),32);std::memcpy(provider.xsa,hw.xsa.data(),32);
  vxl_v2_catalog checked{};int rc=vxl_v2_catalog_open(&checked,p->raw.data(),p->raw.size(),&provider);
  if(rc)return R::failure({-rc,"CATALOG_BIND","original catalog/schema validation rejected"});
  p->bindings.reserve(checked.count);
  for(uint32_t i=0;i<checked.count;i++){
   vxl_v2_catalog_entry e{};std::memcpy(&e,p->raw.data()+64+size_t(i)*512,512);
   CatalogBinding b;b.key.namespace_id=e.namespace_id;b.key.logical_profile=e.logical_profile;b.key.logical_program=e.logical_program;
   std::memcpy(b.key.code.data(),e.code,32);std::memcpy(b.key.schema.data(),e.schema,32);std::memcpy(b.key.hardware_contract.data(),e.hardware_contract,32);
   auto admitted=admit_program(hw,b.key);if(!admitted)return R::failure(*admitted.error);
   for(const auto&old:p->bindings)if(same_key(old.key,b.key))return R::failure({EEXIST,"CATALOG_BIND","ambiguous installed program key"});
   std::memcpy(b.entry_id.data(),e.entry_id,32);b.catalog_sha=p->sha;b.device_generation=hw.device_generation;b.code_bytes=e.code_bytes;b.args_bytes=e.args_bytes;
   p->bindings.push_back(b);
  }
  return R::success(std::shared_ptr<const InstalledCatalog>(new InstalledCatalog(std::move(p))));
 }catch(const std::bad_alloc&){return R::failure({ENOMEM,"CATALOG_BIND","catalog allocation failed"});}
}
Result<CatalogBinding>InstalledCatalog::resolve(const ProgramKey&key)const{
 auto admitted=admit_program(impl_->identity,key);if(!admitted)return Result<CatalogBinding>::failure(*admitted.error);
 for(const auto&b:impl_->bindings)if(same_key(b.key,key))return Result<CatalogBinding>::success(b);
 return Result<CatalogBinding>::failure({ENOENT,"CATALOG_BIND","no exact installed program key"});
}
const BackendIdentity&InstalledCatalog::identity()const{return impl_->identity;}
const Digest&InstalledCatalog::sha()const{return impl_->sha;}
size_t InstalledCatalog::size()const{return impl_->bindings.size();}
}

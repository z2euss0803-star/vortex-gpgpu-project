#pragma once
#include <vxunified/types.hpp>
#include <memory>
namespace vxunified {
struct CatalogBinding {ProgramKey key;Digest entry_id{},catalog_sha{};uint64_t device_generation=0;uint32_t code_bytes=0,args_bytes=0;};
// Trusted startup manifest supplies expected SHA; never an RPC/package field.
// Metadata only. No file opening, program load, address allocation or GPU I/O.
class InstalledCatalog {
 struct Impl;std::unique_ptr<Impl>impl_;explicit InstalledCatalog(std::unique_ptr<Impl>);
public:
 ~InstalledCatalog();
 static Result<std::shared_ptr<const InstalledCatalog>> decode(vxnn::Bytes,const BackendIdentity&,const Digest&expected_catalog_sha);
 Result<CatalogBinding> resolve(const ProgramKey&)const;
 const BackendIdentity&identity()const;const Digest&sha()const;size_t size()const;
};
}

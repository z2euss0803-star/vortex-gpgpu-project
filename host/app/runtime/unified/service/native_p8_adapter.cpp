#include <vxunified/p8.hpp>
#include <vxunified/native_admission.hpp>
namespace vxunified {
Result<std::shared_ptr<P8Adapter>>P8Adapter::create_native(const StartupContext&c,std::shared_ptr<const InstalledCatalog>catalog,std::shared_ptr<const P8Assets>assets,std::shared_ptr<ResidentLease>charge){
 using R=Result<std::shared_ptr<P8Adapter>>;auto admitted=admit_native_startup(c,2);if(!admitted)return R::failure(*admitted.error);auto rt=c.runtime.lock();
 if(!catalog||!assets||!native_identity_equal(catalog->identity(),c.identity)||!rt->owns_resident(charge)||charge->app()!=AppKind::P8||charge->model()!=assets->identity()||charge->bytes()<(128ULL<<20))return R::failure({EXDEV,"P8_ADAPTER","same P8 assets/catalog/common resident lifetime required"});
 auto caps=c.compute_backend->capabilities();if(!caps)return R::failure(*caps.error);return create_bound(std::move(catalog),*caps.value,std::move(assets),c.scope,std::move(charge));
}
}

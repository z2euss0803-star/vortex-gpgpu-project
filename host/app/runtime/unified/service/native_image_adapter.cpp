#include <vxunified/image.hpp>
#include <vxunified/native_admission.hpp>
namespace vxunified {
Result<std::shared_ptr<ImageAdapter>>ImageAdapter::create_native(const StartupContext&c,std::shared_ptr<const InstalledCatalog>catalog,std::shared_ptr<ResidentLease>charge,Digest implementation,Digest reference,bool diagnostic){
 using R=Result<std::shared_ptr<ImageAdapter>>;auto admitted=admit_native_startup(c,1);if(!admitted)return R::failure(*admitted.error);auto rt=c.runtime.lock();
 if(!catalog||!native_identity_equal(catalog->identity(),c.identity)||!rt->owns_resident(charge)||charge->app()!=AppKind::IMAGE||charge->model()!=implementation||charge->bytes()<uint64_t(VX_IMAGE_RESERVED_BYTES)+131072)return R::failure({EXDEV,"IMAGE_ADAPTER","exact common IMAGE cache lifetime and catalog required"});
 auto caps=c.compute_backend->capabilities();if(!caps)return R::failure(*caps.error);return create_bound(std::move(catalog),*caps.value,implementation,reference,c.scope,diagnostic,std::move(charge));
}
}

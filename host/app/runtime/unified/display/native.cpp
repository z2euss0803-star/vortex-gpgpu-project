#include <vxunified/display.hpp>
namespace vxunified {
namespace {struct NativeContext {dp_ops ops{};~NativeContext(){dp_native_free(&ops);}};}
Result<std::shared_ptr<DisplayConsumer>> DisplayConsumer::create_native(std::shared_ptr<ResultStore>store,DisplayComposer compose,DisplayJournal journal,const std::string&device,std::shared_ptr<void>raster_charge){
 if(device.size()!=14||device.compare(0,13,"/dev/dri/card")||device[13]<'0'||device[13]>'9')return Result<std::shared_ptr<DisplayConsumer>>::failure({EINVAL,"DISPLAY_NATIVE","exact DRM card path required"});
 try{
  auto native=std::make_shared<NativeContext>();int rc=dp_native_ops(&native->ops,device.c_str());
  if(rc)return Result<std::shared_ptr<DisplayConsumer>>::failure({-rc,"DISPLAY_NATIVE","original native ops allocation failed"});
  return create(std::move(store),native->ops,vxnn::ClockDomain::LINUX_MONOTONIC,std::move(compose),std::move(journal),native,std::move(raster_charge));
 }catch(const std::bad_alloc&){return Result<std::shared_ptr<DisplayConsumer>>::failure({ENOMEM,"DISPLAY_NATIVE","native context allocation"});}
}
}

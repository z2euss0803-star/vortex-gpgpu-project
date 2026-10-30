#include "display_fixture.hpp"
int main(int argc,char**argv){
 need(argc==2,"native boundary test mode");int retired=0;auto s=store(retired);std::string mode=argv[1];
 if(mode=="path"){
  auto r=DisplayConsumer::create_native(s,composer(),journal(),"/tmp/not-a-DRM-device");need(!r&&r.error->code==EINVAL,"native factory must call original path admission without opening a device");puts("U18_NATIVE_FACTORY_PATH_ADMISSION_PASS_HOST_NOT_DP");return 0;
 }
 auto backing=std::make_shared<FakeDisplay>();std::weak_ptr<FakeDisplay>weak=backing;auto ops=backing->ops();auto d=get(DisplayConsumer::create(s,ops,vxnn::ClockDomain::HOST_MONOTONIC,composer(),journal(),backing),"managed display owner");
 if(mode=="caps"){
  auto c=d->capabilities();need(c.width==4&&c.height==2&&c.dumb&&c.master&&c.connected,"actual selected caps must replace requested dimensions");good(d->restore(),"caps restore");need(!d->capabilities().width,"restored caps unavailable");puts("U18_ACTUAL_SELECTED_CAPS_PASS_HOST_NOT_DP");return 0;
 }
 if(mode=="lifetime-failure"){
  backing->restore_error=-EIO;backing.reset();auto failed=d->restore();need(!failed&&failed.error->code==EIO&&!weak.expired(),"restore failure keeps managed native context");d.reset();need(!weak.expired(),"unknown scanout context retained after owner destruction");puts("U18_UNKNOWN_RESTORE_CONTEXT_PIN_PASS_HOST_NOT_DP");return 0;
 }
 need(mode=="lifetime","lifetime mode");backing.reset();need(!weak.expired(),"KMS context lifetime must follow display owner");good(d->restore(),"managed restore");need(weak.expired(),"known-retired native context released after restore");puts("U18_MANAGED_NATIVE_CONTEXT_LIFETIME_PASS_HOST_NOT_DP");return 0;
}

#include <vxunified/resources.hpp>
namespace vxunified {
static vxl_v2_ref ref(BufferRef b,vxl_v2_ctx& c){
 unsigned domain=0;for(const auto& o:c.objects)if(o.handle==b.handle){domain=o.domain;break;}
 return {b.handle,b.object_generation,b.offset,b.bytes,b.access,domain,b.device_generation};
}
Result<Lease> ResourceTable::acquire(BufferRef b,Consumer k){
 std::lock_guard<std::mutex> lock(mutex_);
 int owner_rc=vxl_v2_owner_check(&context_,owner_,epoch_);
 if(owner_rc || b.device_generation!=epoch_)return Result<Lease>::failure({ESTALE,"lease","owner epoch expired"});
 if(context_.lease_serial==~uint64_t(0))return Result<Lease>::failure({EOVERFLOW,"lease","lease serial exhausted"});
 auto r=ref(b,context_);int rc=vxl_v2_buffer_pin(&context_,owner_,&r,static_cast<uint32_t>(k));
 if(rc)return Result<Lease>::failure({-rc,"lease","invalid, stale or inaccessible object"});
 Lease l;l.token=++context_.lease_serial;l.owner=owner_;l.epoch=epoch_;l.buffer=b;l.consumer=k;
 try{leases_.emplace(l.token,l);}catch(...){vxl_v2_buffer_unpin(&context_,owner_,&r,static_cast<uint32_t>(k));return Result<Lease>::failure({ENOMEM,"lease","lease metadata allocation failed"});}
 return Result<Lease>::success(l);
}
Result<void> ResourceTable::release(Lease l){
 std::lock_guard<std::mutex> lock(mutex_);auto found=leases_.find(l.token);
 if(found==leases_.end())return Result<void>::failure({EBADF,"lease","unknown or already retired"});
 const auto& saved=found->second;const auto& a=saved.buffer;const auto& b=l.buffer;
 if(l.owner!=owner_ || l.epoch!=epoch_ || l.consumer!=saved.consumer || a.handle!=b.handle || a.object_generation!=b.object_generation || a.device_generation!=b.device_generation || a.offset!=b.offset || a.bytes!=b.bytes || a.access!=b.access)return Result<void>::failure({EPERM,"lease","lease identity changed"});
 auto r=ref(saved.buffer,context_);int rc=vxl_v2_buffer_unpin(&context_,owner_,&r,static_cast<uint32_t>(saved.consumer));
 if(rc)return Result<void>::failure({-rc,"lease","object not safely releasable"});
 leases_.erase(found);return Result<void>::success();
}
}

#include <vxunified/optimization.hpp>
#include <map>
#include <mutex>
#include <tuple>
namespace vxunified {
namespace {
auto key_tuple(const CacheKey&k){const auto&h=k.hardware;const auto&p=k.program;return std::tie(h.hardware_contract,h.bit,h.xsa,h.compatible,h.device_generation,h.provider_flags,p.namespace_id,p.logical_profile,p.logical_program,p.code,p.schema,p.hardware_contract,k.model,k.plan,k.constant,k.epoch);}
struct Less {bool operator()(const CacheKey&a,const CacheKey&b)const{return key_tuple(a)<key_tuple(b);}};
bool same(const CacheKey&a,const CacheKey&b){return key_tuple(a)==key_tuple(b);}
bool same(const BufferRef&a,const BufferRef&b){return a.handle==b.handle&&a.object_generation==b.object_generation&&a.offset==b.offset&&a.bytes==b.bytes&&a.access==b.access&&a.device_generation==b.device_generation;}
bool hardware(const BackendIdentity&a,const BackendIdentity&b){return a.hardware_contract==b.hardware_contract&&a.bit==b.bit&&a.xsa==b.xsa&&a.compatible==b.compatible&&a.device_generation==b.device_generation&&a.provider_flags==b.provider_flags;}
}
struct ConstantResidency::Impl {
 struct Entry {BufferRef reference;uint32_t pins=0;};
 BackendIdentity identity;uint64_t max_bytes=0,serial=0;uint32_t max_entries=0;mutable std::mutex mutex;ConstantStats receipt;std::map<CacheKey,Entry,Less>entries;std::map<uint64_t,CacheKey>leases;
 Result<void>valid(const CacheKey&k)const{if(k.epoch!=identity.device_generation||!hardware(identity,k.hardware))return Result<void>::failure({ESTALE,"CONSTANT","hardware/epoch differs"});if(!admit_program(k.hardware,k.program)||!nonzero_digest(k.model)||!nonzero_digest(k.plan)||!nonzero_digest(k.constant))return Result<void>::failure({EINVAL,"CONSTANT","full program/schema/model/plan/content key required"});return Result<void>::success();}
};
ConstantResidency::ConstantResidency(std::unique_ptr<Impl>p):impl_(std::move(p)){}
ConstantResidency::~ConstantResidency()=default;
Result<std::shared_ptr<ConstantResidency>>ConstantResidency::create(const BackendIdentity&h,uint64_t bytes,uint32_t count){
 using R=Result<std::shared_ptr<ConstantResidency>>;if(!h.device_generation||!nonzero_digest(h.hardware_contract)||!nonzero_digest(h.bit)||!nonzero_digest(h.xsa)||!nonzero_digest(h.compatible)||(h.provider_flags!=1&&h.provider_flags!=2&&h.provider_flags!=4)||!bytes||bytes>(1ULL<<30)||!count||count>64)return R::failure({EINVAL,"CONSTANT","bounded sole-owner identity/budget required"});try{auto s=std::make_unique<Impl>();s->identity=h;s->max_bytes=bytes;s->max_entries=count;return R::success(std::shared_ptr<ConstantResidency>(new ConstantResidency(std::move(s))));}catch(const std::bad_alloc&){return R::failure({ENOMEM,"CONSTANT","metadata allocation"});}
}
Result<void>ConstantResidency::remember(const CacheKey&k,BufferRef ref,const Digest&content){
 auto&s=*impl_;std::lock_guard<std::mutex>lock(s.mutex);auto valid=s.valid(k);if(!valid)return valid;
 if(content!=k.constant||!ref.handle||!ref.object_generation||ref.device_generation!=k.epoch||ref.access!=VXL_V2_READ||!ref.bytes||(ref.offset&63)||ref.offset>UINT64_MAX-ref.bytes)return Result<void>::failure({EINVAL,"CONSTANT","checked immutable READ reference required"});
 for(const auto&row:s.entries){if(same(row.second.reference,ref)&&row.first.constant!=content)return Result<void>::failure({EBADMSG,"CONSTANT","same immutable range already binds different content"});}
 if(s.entries.count(k))return Result<void>::failure({EALREADY,"CONSTANT","immutable key already present"});
 if(s.entries.size()>=s.max_entries||ref.bytes>s.max_bytes-s.receipt.bytes)return Result<void>::failure({ENOSPC,"CONSTANT","resident budget exhausted"});
 bool model=false;std::map<Digest,bool>models;try{for(const auto&row:s.entries){models.emplace(row.first.model,true);model|=row.first.model==k.model;}if(!model&&models.size()>=2)return Result<void>::failure({EBUSY,"CONSTANT","two resident model bound"});s.entries.emplace(k,Impl::Entry{ref,0});s.receipt.bytes+=ref.bytes;s.receipt.entries=uint32_t(s.entries.size());return Result<void>::success();}catch(const std::bad_alloc&){return Result<void>::failure({ENOMEM,"CONSTANT","entry allocation"});}
}
Result<ConstantLease>ConstantResidency::retain(const CacheKey&k){
 using R=Result<ConstantLease>;auto&s=*impl_;std::lock_guard<std::mutex>lock(s.mutex);auto valid=s.valid(k);if(!valid)return R::failure(*valid.error);auto found=s.entries.find(k);if(found==s.entries.end()){++s.receipt.misses;return R::failure({ENOENT,"CONSTANT","exact immutable identity not resident"});}if(s.serial==UINT64_MAX||s.leases.size()>=64)return R::failure({ENOSPC,"CONSTANT","bounded lease tokens exhausted"});
 try{ConstantLease lease{s.serial+1,k,found->second.reference};s.leases.emplace(lease.serial,k);s.serial=lease.serial;++found->second.pins;++s.receipt.hits;s.receipt.pins=uint32_t(s.leases.size());return R::success(lease);}catch(const std::bad_alloc&){return R::failure({ENOMEM,"CONSTANT","lease allocation"});}
}
Result<void>ConstantResidency::release(const ConstantLease&lease){auto&s=*impl_;std::lock_guard<std::mutex>lock(s.mutex);auto p=s.leases.find(lease.serial);auto e=s.entries.find(lease.key);if(p==s.leases.end()||e==s.entries.end()||!same(p->second,lease.key)||!same(e->second.reference,lease.reference)||!e->second.pins)return Result<void>::failure({ESTALE,"CONSTANT","exact active token/key/reference required"});--e->second.pins;s.leases.erase(p);s.receipt.pins=uint32_t(s.leases.size());return Result<void>::success();}
Result<void>ConstantResidency::retire(const CacheKey&k,bool confirmed){auto&s=*impl_;std::lock_guard<std::mutex>lock(s.mutex);auto valid=s.valid(k);if(!valid)return valid;auto e=s.entries.find(k);if(e==s.entries.end())return Result<void>::failure({ENOENT,"CONSTANT","no resident key"});if(!confirmed||e->second.pins)return Result<void>::failure({EBUSY,"CONSTANT","all references and backing must be confirmed retired"});s.receipt.bytes-=e->second.reference.bytes;s.entries.erase(e);s.receipt.entries=uint32_t(s.entries.size());return Result<void>::success();}
ConstantStats ConstantResidency::stats()const{std::lock_guard<std::mutex>lock(impl_->mutex);return impl_->receipt;}
}

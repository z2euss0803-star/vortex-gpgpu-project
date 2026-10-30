#include <vxunified/optimization.hpp>
#include <set>
namespace vxunified {
Result<std::vector<uint64_t>>plan_dispatch_order(const OptimizationPolicy&p,const OptimizationBaseline&b,const BackendIdentity&h,const std::vector<ReadyTicket>&tickets,const std::array<uint32_t,3>&quota){
 using R=Result<std::vector<uint64_t>>;auto valid=validate_policy(p,b);if(!valid)return R::failure(*valid.error);
 if(!h.device_generation||!nonzero_digest(h.hardware_contract)||!nonzero_digest(h.bit)||!nonzero_digest(h.xsa)||!nonzero_digest(h.compatible)||(h.provider_flags!=1&&h.provider_flags!=2&&h.provider_flags!=4)||tickets.size()>32)return R::failure({EINVAL,"DISPATCH_PLAN","bounded ready window and provider required"});
 for(auto n:quota){if(!n||n>8)return R::failure({EINVAL,"DISPATCH_PLAN","bounded positive quotas1..8 required"});}
 try{
  std::set<uint64_t>seen;std::array<std::vector<uint64_t>,3>queues;std::vector<uint64_t>out;out.reserve(tickets.size());
  for(const auto&t:tickets){unsigned app=unsigned(t.app);bool allowed=(s8_joint_platform(h)&&(t.app==AppKind::NN||t.app==AppKind::P8||t.app==AppKind::IMAGE))||(h.provider_flags==1&&(t.app==AppKind::NN||t.app==AppKind::IMAGE))||(h.provider_flags==2&&t.app==AppKind::P8);if(!allowed||!t.cookie||!seen.insert(t.cookie).second)return R::failure({EINVAL,"DISPATCH_PLAN","unsupported application or duplicate ready cookie"});if(!p.weighted_dispatch)out.push_back(t.cookie);else queues[app-1].push_back(t.cookie);}
  if(p.weighted_dispatch){std::array<size_t,3>at{};while(out.size()<tickets.size()){for(unsigned app=0;app<3;app++){for(unsigned n=0;n<quota[app]&&at[app]<queues[app].size();n++){out.push_back(queues[app][at[app]++]);}}}}
  return R::success(std::move(out));
 }catch(const std::bad_alloc&){return R::failure({ENOMEM,"DISPATCH_PLAN","metadata allocation"});}
}
Result<void>validate_policy(const OptimizationPolicy&p,const OptimizationBaseline&b){
 if(!nonzero_digest(b.model)||!nonzero_digest(b.plan)||!nonzero_digest(b.input)||!nonzero_digest(b.numeric_reference)||!nonzero_digest(b.accepted_receipt))return Result<void>::failure({EINVAL,"OPTIMIZATION","frozen model/plan/input/numeric baseline and accepted receipt required"});
 if(unsigned(p.reuse_constants)+unsigned(p.batch_submit)+unsigned(p.weighted_dispatch)+unsigned(p.eliminate_redundant_layout)>1){
  return Result<void>::failure({EINVAL,"OPTIMIZATION","one independently attributable switch only"});
 }
 return Result<void>::success();
}
Result<std::vector<CopySpan>>plan_dirty_copies(const OptimizationPolicy&p,const OptimizationBaseline&b,const std::vector<CopySpan>&input,uint64_t source,uint64_t destination){
 using R=Result<std::vector<CopySpan>>;auto valid=validate_policy(p,b);if(!valid)return R::failure(*valid.error);if(input.size()>4096||!source||!destination)return R::failure({EINVAL,"COPY_PLAN","bounded distinct immutable-source copy plan required"});
 try{std::vector<CopySpan>out;out.reserve(input.size());for(const auto&s:input){if(!s.bytes||s.source_offset>source||s.bytes>source-s.source_offset||s.destination_offset>destination||s.bytes>destination-s.destination_offset)return R::failure({ERANGE,"COPY_PLAN","dirty span outside owner-validated extents"});if(p.eliminate_redundant_layout&&!out.empty()){auto&last=out.back();if(last.source_offset+last.bytes==s.source_offset&&last.destination_offset+last.bytes==s.destination_offset){last.bytes+=s.bytes;continue;}}out.push_back(s);}return R::success(std::move(out));}catch(const std::bad_alloc&){return R::failure({ENOMEM,"COPY_PLAN","metadata allocation"});}
}
Result<std::vector<std::vector<uint64_t>>>plan_submission_batches(const OptimizationPolicy&p,const OptimizationBaseline&b,const std::vector<uint64_t>&cookies,uint32_t bound){
 using R=Result<std::vector<std::vector<uint64_t>>>;auto valid=validate_policy(p,b);if(!valid)return R::failure(*valid.error);if(!bound||bound>32||cookies.size()>32)return R::failure({EINVAL,"BATCH_PLAN","one bounded pending32 window required"});
 try{std::set<uint64_t>seen;for(auto n:cookies){if(!n||!seen.insert(n).second)return R::failure({EINVAL,"BATCH_PLAN","distinct nonzero cookies required"});}std::vector<std::vector<uint64_t>>out;size_t width=p.batch_submit?bound:1;for(size_t i=0;i<cookies.size();i+=width){size_t end=std::min(cookies.size(),i+width);out.emplace_back(cookies.begin()+i,cookies.begin()+end);}return R::success(std::move(out));}catch(const std::bad_alloc&){return R::failure({ENOMEM,"BATCH_PLAN","metadata allocation"});}
}
}

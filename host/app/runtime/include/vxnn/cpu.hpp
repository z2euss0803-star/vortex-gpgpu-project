#pragma once
#include <vxnn/types.hpp>
#include <atomic>
#include <functional>
#include <memory>
namespace vxunified {
class PendingBudget {
 struct State;std::shared_ptr<State> state_;
public:
 PendingBudget();vxnn::Result<uint64_t> reserve(uint32_t);vxnn::Result<void> release(uint64_t);uint32_t used()const;
 vxnn::Result<void> claim(uint64_t,uint32_t);vxnn::Result<void> unclaim(uint64_t);
};
std::shared_ptr<PendingBudget> process_pending_budget();
}
namespace vxnn {
using RangeFn=std::function<Result<void>(size_t,size_t)>;
struct CpuTiming {uint64_t begin_ns=0,end_ns=0,total_ns=0,queue_wait_ns=0,compute_ns=0,validate_ns=0,copy_ns=0;uint32_t workers=0;};
struct CpuCancel {std::atomic<bool> cancelled{false};void cancel(){cancelled.store(true);}};
class CpuPool {
 struct State;std::shared_ptr<State> state_;
 Result<CpuTiming> run_impl(size_t,const RangeFn&,std::shared_ptr<CpuCancel>,uint64_t);
public:
 CpuPool(uint32_t workers,size_t max_queued_tasks,std::shared_ptr<vxunified::PendingBudget> = vxunified::process_pending_budget());
 ~CpuPool();CpuPool(const CpuPool&)=delete;CpuPool&operator=(const CpuPool&)=delete;
 Result<void> ready()const;uint32_t workers()const;
 Result<CpuTiming> run_ranges(size_t,const RangeFn&,std::shared_ptr<CpuCancel> = {});
 Result<CpuTiming> run_ranges_reserved(size_t,const RangeFn&,std::shared_ptr<CpuCancel>,uint64_t);
 Result<void> shutdown();
};
Result<CpuTiming> gelu_erf_pool(CpuPool&,const float*,float*,size_t,std::shared_ptr<CpuCancel> = {});
Result<CpuTiming> gelu_erf_pool_reserved(CpuPool&,const float*,float*,size_t,std::shared_ptr<CpuCancel>,uint64_t);
}

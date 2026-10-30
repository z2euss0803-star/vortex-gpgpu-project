#pragma once
#include <vxunified/work.hpp>
#include <vxnn/cpu.hpp>
#include <functional>
namespace vxunified {
// Trusted adapters lower immutable, admitted plans lazily. No hardware work here.
class WorkSource {
public:
 virtual ~WorkSource()=default;
 virtual uint64_t size()const=0;
 virtual Digest identity()const=0;
 virtual Result<WorkItem> get(uint64_t index)const=0;
 // Trusted bounded metadata only: no I/O, allocation of device objects or
 // reentry into Runtime/Scheduler. False applies normal residency backpressure.
 virtual Result<bool> ready(uint64_t)const{return Result<bool>::success(true);}
};
struct SchedulerStats {uint32_t requests=0,indexed=0,active_gpu=0,active_cpu=0,pending=0,completions=0,reserved_completions=0;bool closing=false;};
class Scheduler {
 struct State;std::shared_ptr<State> state_;
public:
 explicit Scheduler(std::shared_ptr<PendingBudget> = process_pending_budget(),std::function<uint64_t()> clock={},bool whole_requests=false);
 ~Scheduler();Scheduler(const Scheduler&)=delete;Scheduler&operator=(const Scheduler&)=delete;
 Result<RequestKey> submit(const WorkRequest&);
 Result<WorkItem> next_ready();
 // prepare is trusted, bounded and non-reentrant. Its allocations finish
 // before activation; output handoff and activation then cannot allocate.
 Result<void> dispatch_into(WorkItem&,std::function<Result<void>(const WorkItem&)> prepare={});
 Result<void> complete(const WorkOutcome&);
 Result<void> cancel(RequestKey);
 Result<RequestStatus> status(RequestKey)const;
 Result<int> request_error(RequestKey)const;
 Result<WorkOutcome> take_completion();
 Result<void> forget(RequestKey);
 Result<void> shutdown();
 SchedulerStats stats()const;
};
}

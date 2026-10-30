#pragma once
#define VXNN_ARENA_INPUT_SESSION_V1 1
#include <vxnn/arena.hpp>
#include <vxnn/backend.hpp>
#include <memory>
namespace vxnn {
struct RequestInput;
enum class ArenaStoragePhase {PREPARED,ALLOCATING,READY,RELEASING,RETIRED,FAULTED};
struct ArenaStorageReceipt {
 ArenaStoragePhase phase=ArenaStoragePhase::PREPARED;
 // Durations cover backend BUFFER_CREATE / release calls, not whole startup.
 uint64_t epoch=0,prefix_bytes=0,backing_bytes=0,allocate_ns=0,release_ns=0;
 bool allocation_attempted=false,release_attempted=false,borrowed_input=false;
  uint32_t input_views=0;
 std::optional<BufferRef> root;std::optional<Error> first_error;
};
// S8 NN single-PL-arena materialization. The planned low8MiB exclusion becomes
// unused bytes INSIDE a real DATA object. No physical-address query or fixed
// allocator is introduced. Exact root-relative offsets still reach v2 COPY and
// typed relocation. The caller must load the trusted program before allocate.
// Metadata lifetime does not release fd-owned resources. Explicit release is
// legal only after all consumers retire; kernel pins remain authoritative.
// Any failed/ambiguous allocation/release is sticky and retained for the owner.
class ArenaStorage {
 struct Impl;std::unique_ptr<Impl> impl_;
 explicit ArenaStorage(std::unique_ptr<Impl>);
public:
 ~ArenaStorage();
 static Result<std::shared_ptr<ArenaStorage>> create(const Plan&,const ArenaPlan&,uint64_t epoch,IGpuBackend&);
 // Derive an input-bound view internally from the captured immutable Plan.
 // Exactly one view may borrow a READY owning root; no arbitrary Plan/layout.
 static Result<std::shared_ptr<ArenaStorage>> borrow_input(const std::shared_ptr<ArenaStorage>& owner,const RequestInput&);
 // Metadata retirement only, after all current consumers have retired.
 Result<void> retire_input_view(bool consumers_retired);
 const Plan&plan()const;
 Result<void> allocate();Result<void> release();
 const ArenaPlan& layout()const;
 const std::vector<ArenaAllocation>& allocations()const;
 const std::optional<ArenaBindings>& bindings()const;
 const ArenaStorageReceipt& receipt()const;
};
}

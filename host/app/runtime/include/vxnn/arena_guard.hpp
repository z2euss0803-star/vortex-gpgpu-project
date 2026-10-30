#pragma once
#include <vxnn/arena.hpp>
#include <vxnn/backend.hpp>
#include <memory>
namespace vxnn {
struct ArenaGuardFault {
 NodeId node=0;TensorId tensor=0;bool workspace=false,trailing=false;
 BufferRef guard;std::array<uint8_t,64> expected{},observed{};
};
struct ArenaGuardReceipt {
 uint64_t seed_ns=0,check_ns=0,writes=0,reads=0,completed_nodes=0,retired_batches=0;
 bool initialized=false,active=false,final_verified=false;
 std::optional<Error> first_error;std::optional<ArenaGuardFault> fault;
 // CPU/Linux intervals only. GPU raw cycles come from the execution receipt.
};
// Owns guard metadata, never owns/frees the borrowed backend or device objects.
// Single owner thread only. Does not allocate storage, submit jobs or ACK.
// verify_retired_node must run after GPU ACK/reap or CPU pool COMMIT; the
// controlled backend additionally refuses COPY while a device object is pinned.
class ArenaGuards {
 struct Impl;std::unique_ptr<Impl> impl_;
 explicit ArenaGuards(std::unique_ptr<Impl>);
public:
 ~ArenaGuards();
 static Result<std::shared_ptr<ArenaGuards>> create(const Plan&,const ArenaPlan&,const std::vector<ArenaAllocation>&,uint64_t epoch,IGpuBackend&);
 Result<void> initialize();
 Result<void> begin_node(NodeId);
 // Intermediate ACK checks keep the logical node and all data lifetimes active.
 Result<void> verify_retired_subjob(NodeId);
 Result<void> verify_retired_node(NodeId);
 Result<void> verify_final();
 const ArenaGuardReceipt& receipt()const;
};
}

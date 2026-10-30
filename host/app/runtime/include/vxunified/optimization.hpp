#pragma once
#include <vxunified/types.hpp>
#include <memory>
namespace vxunified {
struct OptimizationPolicy {bool reuse_constants=false,batch_submit=false,weighted_dispatch=false,eliminate_redundant_layout=false;};
struct OptimizationBaseline {Digest model{},plan{},input{},numeric_reference{},accepted_receipt{};};
Result<void> validate_policy(const OptimizationPolicy&,const OptimizationBaseline&);
struct CopySpan {uint64_t source_offset=0,destination_offset=0,bytes=0;};
// Logical plans only. The sole owner still validates and executes each copy
// and each job; none of these APIs bypass kernel ranges, ACK or retirement.
Result<std::vector<CopySpan>> plan_dirty_copies(const OptimizationPolicy&,const OptimizationBaseline&,const std::vector<CopySpan>&,uint64_t source_bytes,uint64_t destination_bytes);
Result<std::vector<std::vector<uint64_t>>> plan_submission_batches(const OptimizationPolicy&,const OptimizationBaseline&,const std::vector<uint64_t>&cookies,uint32_t bound=32);
struct ReadyTicket {AppKind app=AppKind::NN;uint64_t cookie=0;};
Result<std::vector<uint64_t>> plan_dispatch_order(const OptimizationPolicy&,const OptimizationBaseline&,const BackendIdentity&,const std::vector<ReadyTicket>&,const std::array<uint32_t,3>&quota={1,1,1});
struct CacheKey {BackendIdentity hardware;ProgramKey program;Digest model{},plan{},constant{};uint64_t epoch=0;};
struct ConstantLease {uint64_t serial=0;CacheKey key;BufferRef reference;};
struct ConstantStats {uint64_t bytes=0,hits=0,misses=0;uint32_t entries=0,pins=0;};
// Borrowed immutable READ references from the sole owner's existing allocator.
// This cache never opens a device, allocates device memory, writes weights or
// frees backing. Owner/allocator budgets and kernel checks remain independent.
class ConstantResidency {
 struct Impl;std::unique_ptr<Impl>impl_;explicit ConstantResidency(std::unique_ptr<Impl>);
public:
 ~ConstantResidency();
 static Result<std::shared_ptr<ConstantResidency>> create(const BackendIdentity&,uint64_t max_bytes,uint32_t max_entries=64);
 Result<void> remember(const CacheKey&,BufferRef,const Digest&checked_content);
 Result<ConstantLease> retain(const CacheKey&);
 Result<void> release(const ConstantLease&);
 Result<void> retire(const CacheKey&,bool backing_confirmed_retired);
 ConstantStats stats()const;
};
}

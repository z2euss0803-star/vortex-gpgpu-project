#pragma once
#include <vxunified/types.hpp>
#include <vxnn/backend.hpp>
#include <memory>
namespace vxunified {
class IoctlTransport {
public:
 virtual ~IoctlTransport()=default;
 virtual int call(unsigned long,void *)=0; // 0 or negative errno; no automatic retry
 virtual uint64_t now_ns()=0;
};
class FdTransport final:public IoctlTransport {
 int fd_; // borrowed unique-owner fd; service closes it
public:
 explicit FdTransport(int fd):fd_(fd){}
 int call(unsigned long,void *) override;
 uint64_t now_ns() override;
};
class UnifiedBackend:public vxnn::IGpuBackend {
public:virtual Result<BackendIdentity> identity()=0;
};
class ComputeV2Backend final:public UnifiedBackend {
 struct Impl;std::unique_ptr<Impl> impl_;
public:
 explicit ComputeV2Backend(IoctlTransport&);~ComputeV2Backend() override;
 // Explicitly weaker content policy; only negotiable before a submit attempt.
 Result<void> set_fast_runtime(bool);bool fast_runtime() const override;
 Result<BackendIdentity> identity() override;Result<DeviceCaps> capabilities() override;
 Result<BufferRef> allocate(uint64_t,vxnn::AddressDomain,uint32_t) override;
 Result<void> copy(BufferRef,vxnn::Bytes) override;
 Result<void> copy(BufferRef,vxnn::MutableBytes) override;
 Result<void> copy(BufferRef,BufferRef) override;
 Result<void> release_buffer(BufferRef) override;
 Result<ProgramRef> program_load(const Digest& catalog_entry_id) override;
 // Read the already-admitted immutable catalog binding, without a new ioctl.
 Result<ProgramRef> program_identity(const JobDesc&);
 Result<void> release_program(ProgramRef) override;
 // deadline_ns is absolute Linux CLOCK_MONOTONIC; converted to remaining budget.
 Result<uint64_t> submit(const JobDesc&) override;
 // Metadata-only identity of a retained submission, including ambiguous ioctl
 // errors. One backend/transport belongs to the sole owner's complete fd lifetime.
 Result<uint64_t> submitted_wire_cookie(uint64_t logical_cookie);
 Result<void> wait(uint64_t cookie,uint64_t timeout_ns) override;
 Result<Completion> read_completion(uint64_t sequence) override;
 Result<void> ack_reap(uint64_t sequence) override;
 Result<vxl_v2_completion> detail(uint64_t sequence,uint64_t cookie=0);
};
}

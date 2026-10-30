#pragma once
#include <vxnn/types.hpp>
#include <vxl/demo_runner.h>
namespace vxnn {
class EvidenceSink;
class IGpuBackend {
public:
 virtual ~IGpuBackend()=default;
 // Trusted service policy. Legacy and test backends retain strict content checks.
 virtual bool fast_runtime()const{return false;}
 virtual Result<DeviceCaps> capabilities()=0;
 virtual Result<BufferRef> allocate(uint64_t bytes,AddressDomain,uint32_t access)=0;
 virtual Result<void> copy(BufferRef,Bytes)=0;
 virtual Result<void> copy(BufferRef,MutableBytes)=0;
 virtual Result<void> copy(BufferRef source,BufferRef destination)=0;
 virtual Result<void> release_buffer(BufferRef)=0;
 virtual Result<ProgramRef> program_load(const Digest&)=0;
 virtual Result<void> release_program(ProgramRef)=0;
 virtual Result<uint64_t> submit(const JobDesc&)=0;
 virtual Result<void> wait(uint64_t cookie,uint64_t timeout_ns)=0;
 virtual Result<Completion> read_completion(uint64_t sequence)=0;
 virtual Result<void> ack_reap(uint64_t sequence)=0;
};
// Fixed graph adapter deliberately does not derive from IGpuBackend.
class LegacyBackend {
 demo_session &session_;Path release_root_;int first_error_=0;uint64_t next_event_=1;
 Result<LegacyResult> run(const LegacyInput&,EvidenceSink&,demo_app);
public:
 explicit LegacyBackend(demo_session &session,Path release_root):session_(session),release_root_(std::move(release_root)){}
 Result<LegacyResult> run_hunet(const LegacyInput&,EvidenceSink&);
 Result<LegacyResult> run_mosaic(const LegacyInput&,EvidenceSink&);
};
}

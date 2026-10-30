#pragma once
#include <vxunified/types.hpp>
#include <vxnn/evidence.hpp>
#include <functional>
#include <map>
#include <memory>
namespace vxunified {
enum class TimingPhase:uint32_t {QUEUE,CPU_PREPARE,CPU_GELU,CPU_BIN,PROGRAM_LOAD,WEIGHT_LOAD,TEXTURE_LOAD,INPUT_LOAD,H2D,D2H,GUARD,HASH,SUBMIT,LOCK_WAIT,ACTIVE_POLL,SLEEP,ACK,REAP,CONVERT,KMS_COPY,KMS_FLIP,KMS_RETIRE,END_TO_END,COUNT};
enum class CounterKind:uint32_t {NONE,GPU_JOB,ZERO_JOB_CACHE};
struct EventContext {
 AppKind app=AppKind::NN;BackendIdentity hardware;ProgramKey program;
 Digest model{},plan{},source{},input{};RequestKey request;std::string run_id;
 TimingPhase phase=TimingPhase::END_TO_END;CounterKind counter=CounterKind::NONE;
 std::optional<vxnn::Error> execution_error;
};
struct BoundEvent {Event event;EventContext context;};
struct PhaseTiming {uint64_t inclusive_ns=0,exclusive_ns=0,count=0;};
struct TimingSummary {uint64_t wall_ns=0,exclusive_work_ns=0;std::map<TimingPhase,PhaseTiming> phases;std::optional<uint64_t> gpu_raw_cycles;bool gpu_cycles_complete=false;};
const char* timing_phase_name(TimingPhase);
Result<std::string> serialize_event(const BoundEvent&);
Result<TimingSummary> summarize_timing(const std::vector<BoundEvent>&);
// The trusted sink writes one immutable atomic record, e.g. the service's
// private EvidenceJournal. It is never supplied by an RPC peer. Retry persists
// the retained exact bytes; it never repeats execution or device submission.
using PersistEvent=std::function<Result<void>(const std::string&)>;
struct EventSinkStats {uint64_t emitted=0;bool pending=false;std::optional<vxnn::Error> first_error;};
class UnifiedEvidenceSink final:public vxnn::EvidenceSink {
 struct Impl;std::shared_ptr<Impl>impl_;explicit UnifiedEvidenceSink(std::shared_ptr<Impl>);
public:
 ~UnifiedEvidenceSink();
 static Result<std::shared_ptr<UnifiedEvidenceSink>> create(EventContext,PersistEvent);
 Result<void> emit(const Event&)override;
 Result<void> retry_evidence();EventSinkStats stats()const;
};
}

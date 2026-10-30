#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#if defined(__GNUC__) && __GNUC__ < 8
#include <experimental/filesystem>
#else
#include <filesystem>
#endif
namespace vxnn {
using Digest=std::array<uint8_t,32>;
using Id128=std::array<uint8_t,16>;
#if defined(__GNUC__) && __GNUC__ < 8
using Path=std::experimental::filesystem::path; // Host GCC7 compatibility only.
#else
using Path=std::filesystem::path;
#endif
using Shape=std::vector<uint64_t>;
using TensorId=uint32_t;using NodeId=uint32_t;
struct Bytes {const uint8_t *data=nullptr;size_t size=0;};
struct MutableBytes {uint8_t *data=nullptr;size_t size=0;};
struct Error {
 int code=0;std::string category,detail;
 Error()=default;
 // Parameter temporaries own their storage if a later string allocation fails.
 // GCC7 host O2/no-elide aggregate construction leaked in the isolated OOM probe.
 Error(int c,std::string cat={},std::string det={})
  :code(c),category(std::move(cat)),detail(std::move(det)){}
};
template<class T> class Result {
 struct SuccessTag{};struct FailureTag{};
 Result(SuccessTag,T v):value(std::move(v)),error(std::nullopt){}
 Result(FailureTag,Error e):value(std::nullopt),error(std::move(e)){}
public:
 const std::optional<T> value;const std::optional<Error> error;
 static Result success(T v){return Result(SuccessTag{},std::move(v));}
 static Result failure(Error e){return Result(FailureTag{},std::move(e));}
 explicit operator bool() const {return value.has_value();}
};
template<> class Result<void> {
 explicit Result(bool v):ok(v),error(std::nullopt){}
 explicit Result(Error e):ok(false),error(std::move(e)){}
public:
 const bool ok;const std::optional<Error> error;
 static Result success(){return Result(true);}
 static Result failure(Error e){return Result(std::move(e));}
 explicit operator bool() const{return ok;}
};
enum class NumericClass {PASS,KNOWN_NUMERIC_DEVIATION,NEW_MISMATCH,NOT_EVALUATED};
enum class AddressDomain:uint32_t {CPU=0,PS=1,PL=2};
enum class ClockDomain {LINUX_MONOTONIC,HOST_MONOTONIC,B_MONOTONIC,GPU_COUNTER};
enum class EvidenceScope {HOST_PROTOCOL_TEST,OFFLINE,CROSS_COMPILE,IMAGE,REAL_BOARD};
struct DeviceCaps {
 uint64_t ps_base=0,ps_bytes=0,pl_base=0,pl_bytes=0,alignment=64;
 uint32_t max_objects=64,max_pending=32,max_completions=64,contexts=1;
 uint32_t max_programs=16,max_refs_per_job=8,max_args_bytes=4096;
 uint32_t ps_dma_bits=0,gpu_address_bits=0,pl_address_bits=0,cycle_counter_bits=0;
 uint64_t max_program_bytes=8u*1024u*1024u,max_execution_budget_ns=60000000000ULL;
 bool preempt=false;
};
struct BufferRef {uint64_t handle=0,object_generation=0,offset=0,bytes=0;uint32_t access=0;uint64_t device_generation=0;};
struct ProgramRef {uint64_t handle=0,program_generation=0,device_generation=0;Digest program_sha{};};
struct JobDesc {
 uint64_t program_handle=0,program_generation=0,device_generation=0,cookie=0,deadline_ns=0;
 BufferRef args;std::vector<BufferRef> references;
 // Trusted operator metadata only: if the sealed private payload repeats its
 // cookie, snapshot this exact 64-bit scalar as the actual wire cookie. Never
 // patch the caller's DATA object or infer an offset from opaque payload bytes.
 std::optional<uint32_t> wire_cookie_arg_offset;
};
// cookie identifies logical work. wire_cookie records the original kernel
// submission identity; it may differ when prepared requests dispatch out of order.
// This userspace type is not a kernel UAPI or serialized wire structure.
struct Completion {uint64_t cookie=0,sequence=0;uint32_t status=0;int result=0;bool cycles_valid=false;std::optional<uint64_t> raw_cycles;std::vector<uint64_t> timing_events;uint64_t wire_cookie=0;};
struct RequestKey {std::string boot_id,client_id,request_id;};
struct Event {
 std::string run_id,boot_id,request_id,model_id,plan_id,operator_id,job_id,source_id,host;
 uint64_t event_id=0,device_generation=0,buffer_generation=0,bytes=0;
 std::optional<uint64_t> parent_event,start_ns,end_ns;
 ClockDomain clock_domain=ClockDomain::HOST_MONOTONIC;
 EvidenceScope scope=EvidenceScope::HOST_PROTOCOL_TEST;
 int status=0;bool cycles_valid=false;std::optional<uint64_t> raw_cycles;
};
struct LegacyInput {
 int borrowed_fd=-1;Path release_root;Bytes input;Shape shape;std::string run_id,raw_request,model_id,plan_id;
 Digest input_sha{};bool diagnostics=false;EvidenceScope scope=EvidenceScope::HOST_PROTOCOL_TEST;
};
struct LegacyResult {std::string raw_json;std::vector<std::string> raw_artifacts;int result=0;NumericClass numeric_class=NumericClass::NOT_EVALUATED;};
}

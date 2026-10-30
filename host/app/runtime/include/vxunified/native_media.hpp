#pragma once
#include <vxunified/native.hpp>
#include <vxunified/native_files.hpp>
#include <vxunified/presentation.hpp>
#include <vxunified/image_request.hpp>
#include <vxunified/p8_request.hpp>
namespace vxunified {
struct NativeImageSpec {Digest implementation{},reference{};std::shared_ptr<const std::vector<uint8_t>>readonly_table;uint64_t job_budget_ns=60000000000ULL;};
struct NativeP8Spec {std::string assets_directory;Digest assets{};uint64_t job_budget_ns=60000000000ULL;};
struct NativeImagePrepared {WorkRequest work;std::shared_ptr<ImageWorkRequest>owner;};
struct NativeP8Prepared {WorkRequest work;std::shared_ptr<P8WorkRequest>owner;};
class NativeImageLibrary {
 struct State;std::shared_ptr<State>state_;explicit NativeImageLibrary(std::shared_ptr<State>);
 static Result<void>discard(const std::shared_ptr<ImageWorkRequest>&);
public:
 static constexpr uint64_t resident_bytes=ImageSession::shadow_bytes+131072;
 static constexpr uint64_t input_lifetime_bytes=16ULL<<20,active_scratch_bytes=16ULL<<20;
 static Result<std::shared_ptr<NativeImageLibrary>>create(const StartupContext&,std::shared_ptr<NativeRouter>,std::shared_ptr<ProgramResidency>,std::shared_ptr<NativeFiles>,std::shared_ptr<NativeDisplay>,const NativeImageSpec&);
 Result<NativeImagePrepared>prepare(ImageRequest);
 bool owns_runtime(const Runtime*)const;
};
class NativeP8Library {
 struct State;std::shared_ptr<State>state_;explicit NativeP8Library(std::shared_ptr<State>);
public:
 static constexpr uint64_t resident_bytes=128ULL<<20,frame_lifetime_bytes=256ULL<<20,active_scratch_bytes=64ULL<<20;
 static Result<std::shared_ptr<NativeP8Library>>create(const StartupContext&,std::shared_ptr<NativeRouter>,std::shared_ptr<ProgramResidency>,std::shared_ptr<NativeFiles>,std::shared_ptr<NativeDisplay>,const NativeP8Spec&);
 Result<NativeP8Prepared>prepare(P8Request);
 Result<void>release_session(); // only after original frame ACK/retirement
 Result<void>release_inactive_session(); // worker tick, after joint mode has left P8
 bool owns_runtime(const Runtime*)const;
};
}

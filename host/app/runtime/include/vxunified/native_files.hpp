#pragma once
#include <vxunified/daemon.hpp>
namespace vxunified {
struct NativeFileStats {uint64_t retained_requests=0,reserved_bytes=0,written_bytes=0;};
class NativeRequestFiles {
 struct State;std::shared_ptr<State>state_;explicit NativeRequestFiles(std::shared_ptr<State>);friend class NativeFiles;
public:
 ~NativeRequestFiles();
 Result<void>append_trace(vxnn::Bytes,bool sync=false);
 Result<ResultArtifact>output(const std::string&leaf,vxnn::Bytes,const std::string&layout,const vxnn::Shape&);
};
// One private output/evidence root for all admitted applications, backed by the
// same Runtime CPU budget even on tmpfs. Immutable originals and partial/error
// files are retained; no automatic overwrite, deletion or evidence promotion.
class NativeFiles {
 struct State;std::shared_ptr<State>state_;explicit NativeFiles(std::shared_ptr<State>);friend class NativeRequestFiles;
public:
 static Result<std::shared_ptr<NativeFiles>>create(const StartupContext&,const std::string&private_directory);
 Result<std::shared_ptr<NativeRequestFiles>>request(const RequestKey&,uint64_t trace_limit,uint64_t output_limit);
 Result<std::vector<uint8_t>>read(const ResultArtifact&)const;
 // Directory identity only; callers must still read() to prove the completed
 // immutable artifact belongs to this store and has its original bytes.
 bool belongs_to_request(const RequestKey&,const ResultArtifact&)const;
 NativeFileStats stats()const;
 bool owns_runtime(const Runtime*)const;
};
}

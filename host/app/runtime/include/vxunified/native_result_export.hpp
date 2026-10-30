#pragma once
#include <vxunified/native_files.hpp>
namespace vxunified {
struct NativeMosaicExportSpec {Digest model{},program{},implementation{};std::string build_id;};
struct NativeResultExport {ResultArtifact manifest,tensor;};
// File-only bridge. No target GPU execution, source evidence promotion or
// caller-selected filesystem path; the one registered destination is fixed.
class NativeMosaicExporter {
 struct State;std::shared_ptr<State>state_;explicit NativeMosaicExporter(std::shared_ptr<State>);
public:
 static Result<std::shared_ptr<NativeMosaicExporter>>create(const StartupContext&,std::shared_ptr<NativeFiles>,std::vector<NativeMosaicExportSpec>);
 Result<NativeResultExport>export_result(const PublishedResult&,const std::string&destination);
};
}

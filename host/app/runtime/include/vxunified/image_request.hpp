#pragma once
#include <vxunified/image.hpp>
#include <vxunified/service.hpp>
#include <vxunified/residency.hpp>
namespace vxunified {
class NativeImageLibrary;
struct ImageJobTrace {
 uint64_t index=0,begin_ns=0,end_ns=0,program_ns=0,allocate_ns=0,upload_ns=0,args_ns=0,guard_ns=0;
 uint32_t op=0;int error=0;vxnn::EvidenceScope scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;
 std::optional<uint64_t>gpu_raw_cycles;std::optional<vxl_v2_completion>linux_detail;
};
struct ImageOwnerReceipt {
 uint64_t completed_jobs=0,download_ns=0,publish_ns=0,release_ns=0;
 uint64_t render_begin_ns=0,render_end_ns=0;std::optional<ImageResult>image_result;
 bool materialized=false,retired=false,published=false;
 std::optional<DisplayReceipt>display;std::optional<vxnn::Error>first_error,evidence_error;std::optional<uint64_t>gpu_raw_cycles;
};
struct ImagePresentation {DisplayReceipt display;PublishedResult output;bool failed_display_restored=false;};
using ImageTraceSink=std::function<Result<void>(const RequestKey&,const ImageJobTrace&)>;
using ImageOutputSink=std::function<Result<ImagePresentation>(const WorkRequest&,vxnn::Bytes,const ImageOwnerReceipt&)>;
Result<std::array<uint8_t,512>>encode_image_job_trace(const ImageJobTrace&);
class ImageWorkRequest;
// CPU shadow is private and persists across requests, never a public GPU object.
// Production factory must charge its full lifetime in the common resident
// budget before constructing it (U19/U20); per-request budget is insufficient.
class ImageSession:public std::enable_shared_from_this<ImageSession> {
 struct Impl;std::unique_ptr<Impl>impl_;explicit ImageSession(std::unique_ptr<Impl>);friend class ImageWorkRequest;
 static Result<std::shared_ptr<ImageSession>>create_bound(std::shared_ptr<const InstalledCatalog>,const DeviceCaps&,std::shared_ptr<ProgramResidency>,Digest,Digest,vxnn::Bytes,vxnn::EvidenceScope,bool,const StartupContext*,std::shared_ptr<ResidentLease>);
public:
 ~ImageSession();static constexpr uint64_t shadow_bytes=VX_IMAGE_RESERVED_BYTES;
 static Result<std::shared_ptr<ImageSession>>create_native(const StartupContext&,std::shared_ptr<const InstalledCatalog>,std::shared_ptr<ProgramResidency>,std::shared_ptr<ResidentLease>,Digest implementation,Digest reference,vxnn::Bytes read_table,bool allow_diagnostic=false);
 static Result<std::shared_ptr<ImageSession>>create(std::shared_ptr<const InstalledCatalog>,const DeviceCaps&,std::shared_ptr<ProgramResidency>,Digest implementation,Digest reference,vxnn::Bytes read_table,vxnn::EvidenceScope,bool allow_diagnostic=false);
 Result<std::shared_ptr<ImageWorkRequest>>prepare(const ImageRequest&,uint64_t budget_ns,ImageTraceSink,ImageOutputSink);
};
class ImageWorkRequest final:public WorkSource,public std::enable_shared_from_this<ImageWorkRequest> {
 struct Impl;std::unique_ptr<Impl>impl_;explicit ImageWorkRequest(std::unique_ptr<Impl>);friend class ImageSession;
 friend class NativeImageLibrary;
 Result<void>discard_unsubmitted();
public:
 ~ImageWorkRequest();uint64_t size()const override;Digest identity()const override;Result<WorkItem>get(uint64_t)const override;Result<bool>ready(uint64_t)const override;
 WorkRequest request()const;ExecutionHooks handlers();const vxnn::Plan&plan()const;const ImageOwnerReceipt&receipt()const;
 bool matches_input(const ImageRequest&)const;Result<ImageOwnerReceipt>receipt_snapshot()const;
};
}

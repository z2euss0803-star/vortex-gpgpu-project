#pragma once
#include <vxunified/p8.hpp>
#include <vxunified/residency.hpp>
#include <vxunified/service.hpp>
#include <vxunified/p8_session.hpp>
namespace vxunified {
struct P8Request {RequestKey request;SceneState scene;uint32_t cookie_namespace=0;uint64_t display_cookie=0;bool numeric_reference=true;std::shared_ptr<const TexturePackage>texture;bool texture_redraw=false;};
struct P8OwnerReceipt {
 uint64_t completed_gpu=0,allocate_ns=0,program_ns=0,upload_ns=0,args_ns=0,guard_ns=0,download_ns=0,bins_ns=0,publish_ns=0,release_ns=0;
 uint64_t upload_bytes=0,args_upload_bytes=0,static_reused_bytes=0;
 bool cache_fast=false,cache_root_reused=false,cache_static_reused=false;
 bool materialized=false,published=false,retired=false;std::optional<uint64_t>gpu_raw_cycles;
 std::optional<DisplayReceipt>display;std::optional<SceneState>next_scene;std::optional<vxnn::Error>first_error,evidence_error;
 bool output_crc_valid=false;uint32_t rgba_crc=0,depth_crc=0;uint64_t digest_ns=0;
};
struct P8Presentation {PublishedResult output;DisplayReceipt display;};
using P8TraceSink=std::function<Result<void>(const RequestKey&,const ExecutionReceipt&,const P8OwnerReceipt&)>;
using P8OutputSink=std::function<Result<P8Presentation>(const WorkRequest&,vxnn::Bytes color,vxnn::Bytes depth,const P8OwnerReceipt&)>;
// Lazy four-step frame: transform -> actual readback/CPU bins -> raster ->
// checked readback and presentation. The only fd is borrowed via shared owner.
class P8WorkRequest final:public WorkSource,public std::enable_shared_from_this<P8WorkRequest> {
 struct Impl;std::unique_ptr<Impl>impl_;explicit P8WorkRequest(std::unique_ptr<Impl>);
 static Result<std::shared_ptr<P8WorkRequest>>create_bound(std::shared_ptr<const InstalledCatalog>,const DeviceCaps&,std::shared_ptr<const P8Assets>,std::shared_ptr<ProgramResidency>,const P8Request&,vxnn::EvidenceScope,uint64_t,P8TraceSink,P8OutputSink,const StartupContext*,std::shared_ptr<ResidentLease>,std::shared_ptr<HostMemoryLease>,std::shared_ptr<P8FrameSession>,uint64_t);
public:
 ~P8WorkRequest();
 static Result<std::shared_ptr<P8WorkRequest>>create(std::shared_ptr<const InstalledCatalog>,const DeviceCaps&,std::shared_ptr<const P8Assets>,std::shared_ptr<ProgramResidency>,const P8Request&,vxnn::EvidenceScope,uint64_t execution_budget_ns,P8TraceSink,P8OutputSink);
 static Result<std::shared_ptr<P8WorkRequest>>create_native(const StartupContext&,std::shared_ptr<const InstalledCatalog>,std::shared_ptr<const P8Assets>,std::shared_ptr<ProgramResidency>,const P8Request&,uint64_t execution_budget_ns,P8TraceSink,P8OutputSink,std::shared_ptr<ResidentLease>,std::shared_ptr<HostMemoryLease>,std::shared_ptr<P8FrameSession> session={});
 uint64_t size()const override;Digest identity()const override;Result<WorkItem>get(uint64_t)const override;Result<bool>ready(uint64_t)const override;
 WorkRequest request()const;ExecutionHooks handlers();Result<P8OwnerReceipt>receipt_snapshot()const;
 bool matches_texture_request(const P8Request&,const BackendIdentity&,vxnn::EvidenceScope)const;
};
}

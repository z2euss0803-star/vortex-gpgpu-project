#pragma once
#include <vxunified/catalog.hpp>
#include <vxunified/work.hpp>
#include "image/pipeline_linux.h"
namespace vxunified {
struct StartupContext;class ResidentLease;
struct ImageRequest {
 RequestKey request;std::shared_ptr<const std::vector<uint8_t>>rgba;
 uint32_t width=1280,height=720,input_id=0,crc=0,cookie_namespace=0;
 image_rect rectangle{};image_params params{};uint64_t display_cookie=0;
};
struct ImageResult {
 RequestKey request;Digest plan{},input_sha{},output_sha{};
 uint32_t gpu_jobs=0,completed_gpu=0,output_crc=0;
 bool no_gpu_work=false,cache_hit=false,gpu_complete=false,displayed=false;
 vxnn::EvidenceScope scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;
 std::optional<uint64_t>gpu_raw_cycles;std::optional<vxnn::Error>first_error;
};
// Stateless CPU input validation CRC; no device or per-pixel ioctl.
uint32_t image_crc32(vxnn::Bytes);
// Owns validated metadata. Runtime owner still performs controlled load/DMA,
// guard/readback, durable raw evidence, KMS and DATA retirement; a Plan alone
// is never an executed/published/presented result.
class ImageAdapter {
 struct Impl;std::unique_ptr<Impl>impl_;explicit ImageAdapter(std::unique_ptr<Impl>);
 friend class ImageWorkRequest;
 // Trusted sole owner only: never-started rollback requires proof that neither
 // device I/O nor CPU shadow mutation occurred. After ACKed work discard caches.
 Result<void>cancel_pending(const RequestKey&,bool never_started);
 static Result<std::shared_ptr<ImageAdapter>>create_bound(std::shared_ptr<const InstalledCatalog>,const DeviceCaps&,Digest,Digest,vxnn::EvidenceScope,bool,std::shared_ptr<ResidentLease>);
public:
 ~ImageAdapter();
 static Result<std::shared_ptr<ImageAdapter>>create(std::shared_ptr<const InstalledCatalog>,const DeviceCaps&,Digest implementation,Digest reference,vxnn::EvidenceScope,bool allow_diagnostic=false);
 static Result<std::shared_ptr<ImageAdapter>>create_native(const StartupContext&,std::shared_ptr<const InstalledCatalog>,std::shared_ptr<ResidentLease>,Digest implementation,Digest reference,bool allow_diagnostic=false);
 Result<vxnn::Plan>prepare(const ImageRequest&);
 Result<ImageResult>accept(const WorkOutcome&);
 Result<ImageResult>present(vxnn::Bytes,const DisplayReceipt&);
 Result<ImageResult>presentation_failed(const DisplayReceipt&,bool state_restored);
 Result<vx_image_plan>recipe()const;
 Result<std::shared_ptr<const std::vector<uint8_t>>>input_snapshot()const;
};
}

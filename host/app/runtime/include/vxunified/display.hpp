#pragma once
#include <vxunified/service.hpp>
extern "C" {
#include "viewer/kms.h"
}
namespace vxunified {
namespace display_detail {struct CanvasCompositionAuthority;}
struct DisplayCanvas {
 uint32_t width=0,height=0;
 std::vector<uint8_t> rgb;
 Digest pixel_sha{},source_artifact{};
 DisplayCanvas()=default;
 // A copied/moved public buffer is outside the compositor's const ownership.
 // Never transfer the seal, including assignment to an existing allocation.
 DisplayCanvas(const DisplayCanvas&o):width(o.width),height(o.height),rgb(o.rgb),pixel_sha(o.pixel_sha),source_artifact(o.source_artifact){}
 DisplayCanvas(DisplayCanvas&&o)noexcept:width(o.width),height(o.height),rgb(std::move(o.rgb)),pixel_sha(o.pixel_sha),source_artifact(o.source_artifact){o.seal_={};}
 DisplayCanvas&operator=(const DisplayCanvas&o){seal_={};if(this!=&o){width=o.width;height=o.height;rgb=o.rgb;pixel_sha=o.pixel_sha;source_artifact=o.source_artifact;}return *this;}
 DisplayCanvas&operator=(DisplayCanvas&&o)noexcept{seal_={};if(this!=&o){width=o.width;height=o.height;rgb=std::move(o.rgb);pixel_sha=o.pixel_sha;source_artifact=o.source_artifact;o.seal_={};}return *this;}
 bool has_composition_seal()const noexcept{
  return seal_.owner==this&&seal_.data==rgb.data()&&seal_.bytes==rgb.size()&&seal_.width==width&&seal_.height==height&&seal_.pixel_sha==pixel_sha&&seal_.source_artifact==source_artifact;
 }
private:
 friend struct display_detail::CanvasCompositionAuthority;
 // Authority is defined only in compose.cpp. Published shared_ptr<const>
 // ownership forbids writable aliases; this seal is not a const_cast defense.
 struct CompositionSeal {const DisplayCanvas*owner=nullptr;const uint8_t*data=nullptr;size_t bytes=0;uint32_t width=0,height=0;Digest pixel_sha{},source_artifact{};};
 CompositionSeal seal_{};
};
struct RasterComposeStats {
 uint64_t mapping_builds=0,mapping_hits=0,buffer_allocations=0,buffer_reuses=0,identity_frames=0,scaled_frames=0,retained_canvases=0,retained_bytes=0;
};
namespace display_detail {struct RasterCacheState;}
class RasterComposeCache {
 std::shared_ptr<display_detail::RasterCacheState>state_;
public:
 static constexpr uint64_t budget_bytes=8ULL<<20;
 RasterComposeCache();
 Result<std::shared_ptr<const DisplayCanvas>>compose(const PublishedResult&,size_t,vxnn::Bytes,const DisplayLayout&);
 RasterComposeStats stats()const;
};
Result<DisplayLayout> raster_page_layout(const ResultLease&,size_t artifact,uint32_t canvas_width,uint32_t canvas_height);
Result<std::shared_ptr<const DisplayCanvas>> compose_raster_page(const PublishedResult&,size_t artifact,vxnn::Bytes,const DisplayLayout&);
// The view is produced by the existing trusted HUNet decoder, never RPC data.
Result<std::shared_ptr<const DisplayCanvas>> compose_hunet_page(const PublishedResult&,size_t artifact,vxnn::Bytes,const demo_hunet_result&,const DisplayLayout&);
// The trusted composer owns source loading/conversion; never an RPC callback.
// It receives the retained, original producer receipt including numeric scope.
using DisplayComposer=std::function<Result<std::shared_ptr<const DisplayCanvas>>(const PublishedResult&,const DisplayLayout&)>;
struct DisplayObservation {
 DisplayReceipt receipt;
 // Common retained source token; receipt.cookie is the application's logical
 // presentation identity, while raw_events keep the original C/OS cookie.
 uint64_t source_lease_token=0;
 uint64_t raster_cache_budget_bytes=0;RasterComposeStats raster_cache;
 // Joint applications keep their original protocol number while the common
 // owner uses its unique source token. Zero means no separate application ID.
 uint64_t application_cookie=0;
 std::vector<dp_event> raw_events;
 std::optional<vxnn::Error> first_error;
 uint64_t compose_begin_ns=0,compose_end_ns=0,copy_begin_ns=0,copy_end_ns=0;
 uint64_t poll_begin_ns=0,poll_end_ns=0;
 bool pending=false,restored=false;
};
using DisplayJournal=std::function<Result<void>(const DisplayObservation&)>;
class DisplayConsumer {
 struct State;std::shared_ptr<State> state_;
 explicit DisplayConsumer(std::shared_ptr<State>);
public:
 ~DisplayConsumer();DisplayConsumer(const DisplayConsumer&)=delete;DisplayConsumer&operator=(const DisplayConsumer&)=delete;
 // Internal OS injection boundary, not a remote choice of display provenance.
 static Result<std::shared_ptr<DisplayConsumer>> create(std::shared_ptr<ResultStore>,const dp_ops&,vxnn::ClockDomain,DisplayComposer,DisplayJournal,std::shared_ptr<void> context_lifetime={},std::shared_ptr<void> raster_charge={});
 static Result<std::shared_ptr<DisplayConsumer>> create_native(std::shared_ptr<ResultStore>,DisplayComposer,DisplayJournal,const std::string&device="/dev/dri/card0",std::shared_ptr<void> raster_charge={});
 dp_caps capabilities()const;
 Result<DisplayReceipt> present(const ResultLease&,const DisplayLayout&);
 Result<DisplayReceipt> present(const ResultLease&,const DisplayLayout&,uint64_t logical_cookie);
 Result<DisplayReceipt> poll(uint32_t timeout_ms=0);
 Result<void> restore();
 DisplayObservation snapshot()const;
};
}

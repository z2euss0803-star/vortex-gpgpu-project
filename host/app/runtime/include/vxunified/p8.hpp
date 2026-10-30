#pragma once
#include <vxunified/types.hpp>
#include <memory>
#include <vxunified/catalog.hpp>
#include <vxnn/graph.hpp>
namespace vxunified {
struct StartupContext;class ResidentLease;
class TexturePackage;
enum class P8Object:uint32_t {PORCELAIN=0,PAPERBAG=1,TERRACOTTA=2};
// Original P8 control state. CPU state transitions do not prove a GPU frame or
// physical presentation; the trusted renderer advances only after its receipts.
struct SceneState {
 P8Object object=P8Object::PORCELAIN;Digest theme{};
 uint32_t pose=0,view=0,remaining=100,startup_remaining=100,displayed_pose=0;
 uint64_t completed=0,revision=1,gallery_due_ns=0;
 bool paused=false,looping=false,gallery=false,automatic=false,have_displayed=false;
};
Result<SceneState>scene_control(const SceneState&,char command,uint64_t now_ns);
Result<SceneState>scene_after_frame(const SceneState&,uint32_t rendered_pose,bool texture_redraw,uint64_t now_ns);
Result<SceneState>scene_tick(const SceneState&,uint64_t now_ns);
Result<bool>scene_frame_due(const SceneState&);
Result<SceneState>scene_render_state(const SceneState&,bool texture_redraw);
struct P8Geometry {uint32_t vertices=0,faces=0,active_slots=0,width=0,texture_width=0;};
// CPU-owned immutable input bank. A prepared frame is CPU math, not an executed
// GPU frame; the owner must read actual transform output before rebucketing.
struct P8Frame {
 SceneState scene;P8Geometry geometry;uint32_t active_refs=0;
 std::shared_ptr<const TexturePackage>texture;
 std::array<std::vector<uint32_t>,10> buffers;
 std::array<uint32_t,12> matrix{};Digest input_sha{};bool cpu_reference=false;
};
class P8Assets {
 struct Impl;std::shared_ptr<const Impl>impl_;explicit P8Assets(std::shared_ptr<const Impl>);
 friend class P8WorkRequest;
 Result<std::shared_ptr<P8Frame>>prepare_owned(const SceneState&,std::shared_ptr<const TexturePackage>,bool,bool,bool defer_derived=false)const;
 Result<P8Frame>prepare_bound(const SceneState&,std::shared_ptr<const TexturePackage>,bool,bool,bool defer_derived=false)const;
public:
 static Result<std::shared_ptr<const P8Assets>>load(const std::string&directory);
 Result<P8Geometry>geometry(P8Object)const;
 Result<P8Frame>prepare(const SceneState&,bool cpu_reference=false)const;
 Result<P8Frame>prepare_texture(const SceneState&,std::shared_ptr<const TexturePackage>,bool cpu_reference=false,bool redraw=false)const;
 // Trusted GPU input preparation: same full layout/identity, derived data
 // remains empty until independent actual-transform rebin. No CPU oracle.
 Result<P8Frame>prepare_for_device(const SceneState&,std::shared_ptr<const TexturePackage>texture={},bool redraw=false)const;
 Result<void>rebin(P8Frame&,vxnn::Bytes actual_projected,bool active_only=false)const;
 const Digest&identity()const;
};
class P8Adapter {
 struct Impl;std::unique_ptr<Impl>impl_;explicit P8Adapter(std::unique_ptr<Impl>);
 static Result<std::shared_ptr<P8Adapter>>create_bound(std::shared_ptr<const InstalledCatalog>,const DeviceCaps&,std::shared_ptr<const P8Assets>,vxnn::EvidenceScope,std::shared_ptr<ResidentLease>);
public:
 ~P8Adapter();
 static Result<std::shared_ptr<P8Adapter>>create(std::shared_ptr<const InstalledCatalog>,const DeviceCaps&,std::shared_ptr<const P8Assets>,vxnn::EvidenceScope);
 static Result<std::shared_ptr<P8Adapter>>create_native(const StartupContext&,std::shared_ptr<const InstalledCatalog>,std::shared_ptr<const P8Assets>,std::shared_ptr<ResidentLease>);
 Result<vxnn::Plan>prepare_frame(const SceneState&)const;
 Result<vxnn::Plan>prepare_texture_frame(const SceneState&,std::shared_ptr<const TexturePackage>,bool redraw=false)const;
 Result<ProgramKey>program(uint32_t logical_program)const;
};
}

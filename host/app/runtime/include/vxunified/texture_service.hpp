#pragma once
#include <vxunified/texture.hpp>
#include <vxunified/p8_request.hpp>
namespace vxunified {
struct StartupContext;class NativeRouter;
enum class TexturePhase:uint32_t {IDLE=0,RECEIVING=1,STAGED=2,QUEUED=3,APPLIED=4,FAILED=5,CANCELLED=6};
// These are distinct enums. In particular internal APPLIED4 is wire APPLIED3.
uint32_t texture_wire_phase(TexturePhase);
struct TexturePeer {uint32_t address=0;uint16_t port=0;};
struct TextureIngress {uint64_t serial=0;P8Request input;std::shared_ptr<PreparationLease>preparation;};
struct TextureStatus {TexturePhase phase=TexturePhase::IDLE;uint64_t serial=0;uint32_t wire_tx=0;Digest pending{},current{};std::optional<vxnn::Error>first_error;};
// One process-wide owner serializes the frozen C receiver. Local file packages
// and wire uploads use the same pending frame and trusted retirement gate.
class TextureService {
 struct Impl;std::shared_ptr<Impl>impl_;explicit TextureService(std::shared_ptr<Impl>);
 static Result<std::shared_ptr<TextureService>>create_bound(const BackendIdentity&,const std::string&,uint32_t,uint32_t,uint64_t,vxnn::EvidenceScope,const StartupContext*,std::shared_ptr<NativeRouter>);
public:
 ~TextureService();
 static constexpr uint64_t staging_bytes=17825792+65536;
 static Result<std::shared_ptr<TextureService>>create(const BackendIdentity&,const std::string&boot,uint32_t session,uint32_t cookie_base,uint64_t display_base,vxnn::EvidenceScope);
 static Result<std::shared_ptr<TextureService>>create_native(const StartupContext&,std::shared_ptr<NativeRouter>,uint32_t session,uint64_t display_base);
 Result<std::vector<uint8_t>>handle(vxnn::Bytes,TexturePeer,uint64_t now_ms);
 Result<void>poll(uint64_t now_ms);
 Result<TextureIngress>stage(std::shared_ptr<const TexturePackage>,const SceneState&);
 // Native file decoders reserve this shared CPU lease BEFORE making the bank.
 // The alias owns both the bank and charge until the final consumer retires.
 Result<TextureIngress>stage_native(std::shared_ptr<const TexturePackage>,const SceneState&,std::shared_ptr<HostMemoryLease>);
 Result<std::optional<TextureIngress>>acquire(const SceneState&);
 // Called after the sole Runtime::submit; no packet may supply these objects.
 Result<void>commit(uint64_t,std::shared_ptr<P8WorkRequest>,const RequestKey&accepted);
 // Called after execution outside output callbacks, never while P8 owner locks
 // are held. APPLIED requires original GPU ACK and old KMS front retirement.
 Result<SceneState>complete(uint64_t);
 Result<void>cancel(uint64_t);
 Result<TextureStatus>snapshot()const;
 Result<void>close();
};
}

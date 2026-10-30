#pragma once
#include <vxunified/work.hpp>
namespace vxunified {
// Immutable-by-content file import. The original result and FP32 bytes are
// retained beside derived RGB; a later GPU display never upgrades source scope.
struct MosaicResultLease {
 ResultLease lease;std::string original_json,numeric_class;
 Digest result_sha{},tensor_sha{},rgb_sha{};
 std::vector<uint8_t>raw_fp32,rgb;
 uint32_t width=0,height=0;
};
Result<MosaicResultLease>import_result(const std::string&result_json_path);
struct TexturePolicy {std::string package_directory;Digest expected_package_id{};};
// Administrator-sealed local file admission. RPC carries only this identifier;
// source paths, content identities and preprocessing policy are never wire data.
struct RegisteredTextureFile {std::string id,result_path;Digest result_sha{};TexturePolicy policy;};
class TexturePackage;
enum class TextureOrigin {MOSAIC_RESULT,PHOTO_UPLOAD};
Result<std::shared_ptr<const TexturePackage>>make_texture(const MosaicResultLease&,const TexturePolicy&);
class TexturePackage {
 TextureOrigin origin_=TextureOrigin::MOSAIC_RESULT;
 Digest id_{},result_sha_{},tensor_sha_{};vxnn::EvidenceScope scope_=vxnn::EvidenceScope::OFFLINE;
 std::array<std::vector<uint8_t>,2>lods_;std::array<Digest,2>lod_sha_{};std::string metadata_;
 friend Result<std::shared_ptr<const TexturePackage>>make_texture(const MosaicResultLease&,const TexturePolicy&);
 friend Result<std::shared_ptr<const TexturePackage>>admit_uploaded_texture(vxnn::Bytes,vxnn::Bytes);
public:
 TextureOrigin source_kind()const{return origin_;}
 const Digest&identity()const{return id_;}const Digest&source_result_sha()const{return result_sha_;}
 const Digest&source_tensor_sha()const{return tensor_sha_;}vxnn::EvidenceScope source_scope()const{return scope_;}
 vxnn::Bytes lod(unsigned level)const{return level<2?vxnn::Bytes{lods_[level].data(),lods_[level].size()}:vxnn::Bytes{nullptr,0};}
 const std::string&metadata()const{return metadata_;}
 uint64_t cpu_bytes()const{return lods_[0].size()+lods_[1].size()+metadata_.size();}
};
// Complete opaque dualLOD input only. This never asserts neural provenance.
Result<std::shared_ptr<const TexturePackage>>admit_uploaded_texture(vxnn::Bytes small,vxnn::Bytes large);

}

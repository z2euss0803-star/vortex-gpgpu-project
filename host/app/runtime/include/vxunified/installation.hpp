#pragma once
#include <vxunified/native_nn.hpp>
#include <vxunified/texture.hpp>
extern "C" {
#include "viewer/kms.h"
}
namespace vxunified {
// Installation files belong to the service administrator/image, never an RPC
// caller or a package's self-asserted implementation. Hardware excludes the
// boot-local epoch, which must come from the actual common Runtime probe.
struct InstalledHunetView {std::string directory;Digest manifest{};};
struct InstalledDisplay {std::string device;std::optional<InstalledHunetView>hunet;};
struct InstalledEndpoint {
 uint32_t session=0;uint64_t display_base=0,job_budget_ns=0;
 uint16_t port=0;std::string address;
};
struct InstalledImage:InstalledEndpoint {
 Digest implementation{},reference{},readonly_sha{};
 std::string readonly_path,bundle;
};
struct InstalledP8:InstalledEndpoint {std::string assets_directory;Digest assets{};std::vector<RegisteredTextureFile>textures;uint32_t startup_selftest_frames=1;};
struct InstalledMosaicExport {std::string build_id;};
struct NativeInstallation {
 uint32_t version=1;
 Digest sha{},catalog_sha{};std::string catalog_path;
 std::vector<InstalledNnModel>nn;
 std::optional<InstalledDisplay>display;
 std::optional<InstalledImage>image;
 std::optional<InstalledP8>p8;
 std::optional<InstalledMosaicExport>mosaic_export;
};
Result<NativeInstallation>decode_native_installation(const std::string&,const BackendIdentity&,const Digest&expected);
ApplicationBuilder installed_applications_from(const std::string&manifest,const std::string&sha_file);
// Trusted OS test seam, never a manifest/RPC option. It must refuse REAL_BOARD;
// the normal two-argument production entry always uses native DRM operations.
ApplicationBuilder installed_applications_for_host_test(const std::string&manifest,const std::string&sha_file,const dp_ops&,std::shared_ptr<void> lifetime={});
}

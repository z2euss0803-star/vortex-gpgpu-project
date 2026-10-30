#pragma once
#include <vxunified/native_result_export.hpp>
#include <vxunified/texture.hpp>
namespace vxunified {
// Metadata/path binding only. The original import_result/make_texture checks
// still validate source files, pixels, preprocessing and both native LODs.
// The independently recorded trusted B conversion supplies the package hash.
// RPC carries that hash and the original producer; paths remain fixed here.
Result<RegisteredTextureFile>joint_texture_file(const StartupContext&,const PublishedResult&,const NativeResultExport&,const Digest&expected_package);
}

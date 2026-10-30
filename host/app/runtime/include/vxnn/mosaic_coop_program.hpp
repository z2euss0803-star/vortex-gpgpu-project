#pragma once
// Fail closed until the final actual RV32 publication fills all three pins.
// Never place a legacy or intermediate CODE/source hash in these constants.
namespace vxnn {
inline constexpr const char*mosaic_coop_code_sha256="f5b5321b11a238c01db69bc5cee0da34fcd0031ef8382099826e9a307ac8adb4";
inline constexpr const char*mosaic_coop_validator_sha256="a3c165d6a2087ae9037325be1b4cc24773c5a975826aae779f1c4bf2b2c17c2f";
inline constexpr const char*mosaic_coop_work_layout_sha256="bbdc97c53fcc22596bcd3d6885901f8cc5710e4bb3bd9fb18c9903744ade8fbb";
inline constexpr const char*mosaic_coop_schema_sha256="82455fa2e26f497c761da92c8c6c8bb4c2cd73e4c57f7cc9552ceafe0e3e8faf";
inline constexpr const char*mosaic_coop_variant="cooperative-v6";
inline constexpr const char*mosaic_coop_directory="mosaic-int16-v6";
}

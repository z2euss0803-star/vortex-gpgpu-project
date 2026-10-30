#pragma once
struct P8FileIdentity {const char*name;size_t words;const char*sha;};
static constexpr P8FileIdentity p8_files[]={
{"cm_background.bin",65536,"3b7b9a25c37c2d181d4fc66ae8d4fb8fd4d219c8c67d97e0a59ba8022e497078"},
{"cm_qinghua_texture.bin",262144,"0a67d7124a083480aae5300379a888735f725fcbb43638ee8e465740be783de6"},
{"cm_background_high.bin",262144,"fe2498522fd0354b642945150fb00a6b6727eab71e3f2685afe1d15b20f108f9"},
{"cm_qinghua_high_texture.bin",4194304,"12642d24e209cf89eb99797b5f1f884ee0e3d70ff3bc3e1c6c937734a85bf0f7"},
{"cm_porcelain_raw.bin",128512,"1e80bdaeb40428c8f2972a6d593124cf237945ae5b0354d839c5600fdc49cdf4"},
{"cm_porcelain_indices.bin",98304,"a59abe515a1e67a4e7d34f3d48190f281f0239a07f2b647f6a5c72c85391c7ad"},
{"cm_paperbag_raw.bin",128512,"739687c851afcece04dadc596b5f113d332a284e81424dd94679c3072ac93d96"},
{"cm_paperbag_indices.bin",98304,"74cd49e68b88756ba8d356c1642466a607fec00265192e513e5b108e8d6993ea"},
{"cm_warrior_material_rgb.bin",30926,"1b3e111557870129a9e15e9443d52af9e7fb778d3b3954a91829bd3c260e6ffe"},
{"cm_warrior_material_mix.bin",30926,"9a04fa6f5d213533ff9346fad8f903619b60a7cfc75737407b139bc9e7b67ad0"},
{"cm_terracotta_scan_raw.bin",128512,"be8b7780a97741df1efdc36d6b325e68164a469c284427aa98e96116c3136090"},
{"cm_terracotta_scan_indices.bin",98304,"6ec4c81cb32d2f493bb553776a33c5663a3b9992502b5a02d94e435ddc7cc137"},
{"cm_transform_kernel.bin",194,"d177e3fbb5c489dc9699224511e6d3efe746b35179f63b23b67476b8966320a8"},
{"cm_raster_kernel.bin",373,"e8c862565c29604a44ec0dc9a464320841bb790385fd337208ff526513d96b8e"},
{"cm_transform_high_kernel.bin",194,"f7444827893eda8abfad9e1d807c317123d04f6047ae1261b320fdc213d999d7"},
{"cm_raster_high_kernel.bin",398,"6d00b9235f68303962ab30d8793f2dc743dab6545058e5d2393dc3fe76a66908"},
{"cm_matrices.bin",1248,"45f4d098687f3e4dd42835f618abb7016fbd00daf381024235b3bd021a4e5469"},
{"cm_warrior_poses.bin",8,"3375a1485226aab36457dbb099cce54dd70aabf5c8b07e355f41101250f6be95"},
};

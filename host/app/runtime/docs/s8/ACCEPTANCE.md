# S8 layer acceptance

Parent89b2077; approved S8-LINUX-VALIDATION-V1; profile5/program11.

2026-10-18 current source commitadafe79bfc3c925de98eef34c0c530195cb3a8d6, source SHA256 9c34bfb2e940add9954fd437a2ef535cdc39880bb7c5b784b718ff2577305798. Prior489f4fc/c38d173d remains a historical software/cross baseline. Hardware export dependency closed by exact supplied S8 bit/XSA; no physical/board acceptance inherited.

|Layer|Actual state|Evidence / boundary|
|---|---|---|
|SW|PASS affected-source closure|t23/software-acceptance-adafe79.json:843 source files byte-identical to t17/ubuntu-574 full ordinary/ASan/UBSan/legacy exit0 baseline; only image.py/test_image_prepare.py changed and actual10 affected Ubuntu tests3c5f6 pass, post-source9c34 confirmed. Not a repeated full aggregate on modified source.|
|CROSS_DEVICE|PASS static build|t19/ubuntu-310352eca53d42cd9f8c6325d2470cdd: real RV32 ELF/flat36672B; TBSS stride48B, conservative stack1504B <8128B. No target execution.|
|CROSS_CPU|PASS build|same actual run: matching AArch64 library/runner and sysroot/loader audits.|
|CROSS_KERNEL|PRE_BINDING|same actual run: real Linux5.4 headers/config/Module.symvers; unavailable final binding hard-refuses profile5.|
|CROSS_BINDING|PASS bound module build|t19/ubuntu-6f7091503d2f488dab5534c42552cb3a: bindingeda1b80f555a33272bacb13b980f0371bed4b24ec72a7081406d67b1c661bb34, all512B in actual moduleSHAe9345e5f382a9ae2829a7525a2df0ac6c8d6c11ff429a7085199fe209e847854; not builtin image or READY evidence.|
|IMAGE|PASS actual build and content audit|Full native build3646 tasks exit0; first actual audit failed missing BSP registration and is retained. Four separately hashed native BSP configuration inputs restored, incremental rebuild+BOOT packaging exit0. Final t22/image-20261018T105408Z-audit exits0 and verifies actual FIT/rootfs/DT, builtin driver, full512B binding,500 payloads, matching S8 bit and boot script.|
|BOARD_PLATFORM|NOT_RUN|No board connection or device action.|
|BOARD_S8|NOT_RUN|No integer, CRT64 or GPU computation claim.|
|COMPAT_BOARD|NOT_RUN|Host legacy regression does not close old instruction board compatibility.|
|PERF|NOT_RUN|No measured GPU cycles/seconds or flags0/1 pair.|

Actual hardware consumer receipt: t20/hardware-20261018T0812/receipt.json. Source708bfd8b..., routed DCPf550075c..., bit94ae9d3e..., XSA02e68160...; independent bit equals XSA embedded bit. Hardware physical OPEN_NOT_PASS and exporter simulation WAIVED_NOT_RUN retained.

Release build/s8/release-9c34bfb2 is SOFTWARE_BOUND_PACKAGE_NOT_IMAGE,500 payload files. Future S8 BOOT.BIN must use the actual new S8 bit; old W4 BOOT.BIN cannot be retained for this candidate. No SD authorization implied.

GPU raw cycles=null (no board execution); no physical frequency measurement. Hardware OPEN_NOT_PASS is retained. Plan is executing inline from T01. No old HUNet PASS inherited.

Final offline matrix: evidence/s8/t23/final-layer-matrix-9c34bfb2.json. All scoped offline tasks completed; no combined global application/board PASS.

Actual matched boot set in release/S8-LINUX-VALIDATION-V1-20261018T093916Z: BOOT.BIN24828600B SHAecb96e852139fb8ec38da4fb036eb75a82b8258033417ba6aa09e88451744204; boot.scr270B SHA6605bf8d7a075b4ed032b2a33b5e5bca1926f324f29ee5b938dd863c75ab8679; image.ub48233168B SHAfdc5b3405861289861e29f561aeb57d631062d1515e0fc6c5d172c10583cef18. ZIP SHA256 cf3660a40db32b8886dfcd8b4e8a2c274a767735ae279517c79f4958d721a026.

Reproduction explicitly requires image.prepare THEN native BSP configuration supplement build/s8/bsp-build-config-9c34bfb2.zip (SHA2561cb858a392d84aae194ea295f7df0fc2367c03ead28190ea38bab3ed55f547d6) THEN actual build/audit. The supplement preserves four exact parent registration inputs independently of backend source9c34/bindingeda1. It is copied into final delivery and evidence/s8/t22/build-configuration. Fresh/cold builds still require15GiB; the measured BSP-only incremental run used a distinct4GiB extra-disk gate and retained original kernel outputs unchanged. Do not generalize that reduced incremental budget to new source builds.

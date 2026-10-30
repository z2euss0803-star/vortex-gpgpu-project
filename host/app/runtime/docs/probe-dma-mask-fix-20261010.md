# H0 probe DMA-mask correction

This branch derives from frozen source commit `8ec29ebe7be80e1f270502a0e35126e915ab5ed9`. The original repository and delivery archives are preserved.

## Observed failure

Actual first-boot image `H0-LINUX-BACKEND-20261009T081901Z`, Xilinx Linux 5.4.0, reported platform probe error -5 and did not register `/dev/vortex0`. A single-shot query module on that same kernel/device reported `dma_supported(31-bit)=0`, `dma_supported(32-bit)=1`; both configured masks were 0xffffffff. Its intentional -ECANCELED return prevents residency; it did not allocate DMA memory, change masks, map MMIO or transfer data. The external module set kernel taint bit 12. Raw evidence is in `evidence/20261010-probe-fix/`.

The target kernel's `dma_set_mask` returns -EIO when the requested mask is not supported. The frozen driver requested 31 bits. This establishes a deterministic probe barrier; it does not establish success of later gates.

## Change

Request a supported 32-bit streaming/coherent DMA mask. Keep exact reserved-memory allocation at PS DMA address 0x10000000, 1 MiB size, original protected FENCE/bounce blocks, object/range and diagnostic-program checks. Add per-stage probe errors and success identification. Do not change UAPI, DT, PL, boot script or resource ownership.

## Validation

On B Ubuntu native ext4, `make -j2 test-offline test-legacy` exited 0: nine contract tests, core bounds/pools/jobs, 48 copy guard cases, 100 jobs/10 close cycles, program identity, CDMA fault/order cases, public API synthetic protocol mock and legacy dispatch fixture. The public API mock does not execute GPU ISA.

External-module compilation against the actual first-boot Xilinx kernel source/build with ARCH=arm64 and W=1 exited 0. Module SHA256: `24c1bfb49d647d088cdc61c2949d3860ddaa7263530d3f9507cad49cc9672d32`. This module is a compile artifact; the production driver must remain built in and must not be loaded alongside the running built-in driver.

## Remaining gates

Build/audit an independent repaired image, then perform a separately approved normal SD deployment/reboot. Check stage logs, exact PS allocation, ready and GPU identity, misc registration before any functional board tests. Fixed-image probe success, real GPU/CDMA operation, automatic boot-script repair and H1 acceptance are not claimed here.

# Software-visible H0 contract v1.0

Authoritative resources are re-extracted from archived XSA/HWH, checked against input manifest and embedded bit. See profiles/h0-r4.json. ID/config alone never establishes current bit identity. Reference control RTL and its source chain are archived in vendor/h0; r4 source_manifest.json points to resetfix bit/XSA with matching hashes.

GPU 0xa0000000, CDMA 0xb0010000 and ready GPIO 0xb0020000 are three distinct resources. ready is obtained through a GPIO descriptor, never a duplicate MMIO request. Control offsets and masks match gpgpu_hw.h. START=1 only in idle/ready/quiet; ACK=2 only after DONE; FAULT has no accepted RESET. Completion requires jobs_before+1 (u32 wrap), DONE without errors, pending reads/writes zero, hardware drain/FENCE, ACK followed by idle and memory ready. No IRQ on H0 CDMA; 40-bit addresses, 128-bit bus, no SG/DRE. Poll monotonic execution deadline. Byte copies use aligned16 RMW inside 64-byte padded allocation, bounded4096 block policy.

H1 proposal inherits these observable semantics for discussion only. Its XSA/bit/hash, clocks/resets, capability/ISA changes, manifest and accepting owner are absent. It is not a selected production profile. H1 acceptance cannot be inferred from H0 or software tests.

Real PS coherent placement is proposed at 0x10000000/1MiB, with FENCE64 and permanent bounce4096 excluded. Current archived DTB does not reserve it: no production probe/allocation until explicit reviewed DT configuration exists. PS GPU and CDMA DMA domains must agree; bus, GPU, CPU virtual addresses remain distinct. See memory-contract.csv.

# S8 SUBMIT_ENTER FIX1 offline entry

BuildID: `S8-LINUX-VALIDATION-FIX1-20261018T121440Z`.

Production fix commit58abedb77b3490726d6da954d1f356acc712b7a7; sourceSHA bc52c65c80ae83414ec9ac48101f387184679197208b52a8224f0af9395a2b50; full512B bindingSHA dbbb85d65347132a6f62cf8e8cc0009627f2c13ca885c82f09ed90751604555a.

Only new trace activation/event ordering changed; original READY freeze guard retained. Regression includes first/next READY submit, once-only saved real ioctl-entry event and unchanged prior row. Public176B UAPI, profile5/program11/oldIDs1..10 and GPU ELF/flat/catalog/wire/bit/XSA all unchanged.

Actual native RED/GREEN, complete Ubuntu ordinary+sanitizer+legacy software, RV32/AArch64/matching module/full-bound compilation PASS. Actual PetaLinux/BOOT naturally0 after2327.971387s; strict actual image audit0/errors[] after8.826533s. Independent source review found no actionable defect in this fix. Full final evidence `evidence/s8/submit-enter-fix1/FINAL.json`.

Matched A delivery `release/S8-LINUX-VALIDATION-FIX1-20261018T121440Z`, ZIP SHA f20e8543cfb1b6c1e574fd6685999b40e5868f5c45dad1f9f5cc6a27e13ee85b. Same ZIP verified/unpacked on B Windows ordinary project storage: `D:/CodexWork/s8-submit-enter-fix1-bc52c65c-20261018/files`.

| File | Bytes | SHA256 |
|---|---:|---|
| BOOT.BIN | 24828600 | bc0adac002c4a5de55200ca1c92b2f7b1bc5d5770a42482a5056c10966de3e73 |
| boot.scr | 270 | 6605bf8d7a075b4ed032b2a33b5e5bca1926f324f29ee5b938dd863c75ab8679 |
| image.ub | 48233776 | 16fe6e26ca83cadd0b6b657ce1adbd238b928a70eb3eb6de78387184014834c0 |

Native S8 project retains its old directory name9c34bfb2 but active integration/source/binding is now FIX1. Original full image/native inputs were independently archived and completely verified before the five input replacements. Cold builds still use15GiB admission; this actual build used a separately documented4GiB extra incremental budget with unchanged hardware/config/BSP.

Original first-board case1 remains FAIL_TIMING_CONTRACT with raw127273 cycles/mask510. That run had no S8 instruction jobs. FIX1 board case1, actual integer cases/compat and performance are NOT_RUN or NOT_ACCEPTED; physical OPEN_NOT_PASS remains. No new raw GPU cycles exist for this fix candidate.

No SD/board/reset/PL action was taken for this fix task. Notify user before later SD writing, identify actual media and preserve its current boot set first; later real board timing needs a separate authorized new run. Never edit original case1 timestamps or promote its failed timing to PASS.

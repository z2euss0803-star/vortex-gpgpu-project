# CNN candidate identity before device open

The real CLI reads /etc/codex-build-id, /proc/sys/kernel/random/boot_id,
/etc/codex-cnn-manifest.json and uname. Rejected identity creates no
evidence directory and opens no device. The JSON parser rejects duplicate
keys (including escaped spellings), trailing input, malformed syntax,
wrong types and manifests larger than 1 MiB.

The installed identity manifest is a separate image artifact, not the
source-stage CNN_OFFLINE_RELEASE receipt. Required fields:

| Field | Requirement |
|---|---|
| scope | CNN_CANDIDATE_IDENTITY_V1 |
| source_commit | exact 40 lowercase hex compiled into vxl_cnn |
| build_id | exact build-id file, H0-LINUX-CNN-I32-V1-YYYYMMDDTHHMMSSZ |
| contract | cnn-i32-8x8-v1 |
| profile | h0-r4 |
| bit_sha256, xsa_sha256 | frozen h0-r4 contract values |
| args_bytes | integer 64 |
| programs | ordered IDs 2,3,4,5; 4096 bytes, exact active instruction lengths and SHA |

Runtime architecture must be aarch64 and kernel release must be
5.4.0-xilinx-v2020.1. A fresh boot UUID is recorded but cannot approve a
previous boot's result.

These are installed declarations. They do not measure the physical bit
from Linux or establish trust against a malicious root user. Image audit
must bind actual source, kernel, DTB, rootfs, tools and boot package.
After opening the device, the existing real-driver GET_INFO/profile,
ready, allocator and execution checks still apply. Board identity and
actual GPU execution remain separate acceptance gates.

CPU references, RV32 scalar instruction models, native host protocol
tests and AArch64 compilation are never promoted to real-board CNN PASS.

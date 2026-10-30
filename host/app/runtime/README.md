# Vortex H0 Linux backend — isolated implementation

Scope: IC/FPGA system software, based on Vortex v2.3 d425a56, H0 r4 XSA/bit and Xilinx PetaLinux 2020.1/Linux5.4. A=RY owns source. B Ubuntu native ext4 builds a hashed snapshot. Board powered off; no SD, MMIO, real DMA, GPU or bit programming performed.

Read state.json and docs/progress.md for actual gates. Source and contract are not board evidence. The public API test loads the real archived Vortex stub through VORTEX_DRIVER=zynq into libvortex-zynq.so and a explicitly selected CPU/memory protocol mock. Production defaults to /dev/vortex0; no /dev/mem or bare-metal shortcut.

Commands:

```sh
python3 scripts/preflight.py --offline
make help
make test-offline
python3 scripts/collect_evidence.py --out build --run-id my-offline-run
make userspace-cross CROSS_COMPILE=/locked/path/aarch64-xilinx-linux-
make kernel-check KERNEL_SOURCE=/isolated/kernel-source KERNEL_BUILD=/isolated/kernel-build CROSS_COMPILE=/locked/path/aarch64-xilinx-linux-
```

Windows source path: F:/obsidian-codex/codex/work/vortex_linux_backend_20261008.
Ubuntu build path: /home/z2euss/workspace/vortex_linux_backend_20261008.
toolchain.lock.json binds actual GCC9.2, sysroot and original .config/Module.symvers hashes. Kernel-check compiles a .ko solely to verify interfaces; deployment is built-in only, no accepted unload/hotplug path.

Real use still needs reviewed PS reserved memory, clock/reset/ready DT descriptors, generic CDMA owner exclusion, exact boot manifest, deployment authorization and H0 board tests. Generic kernels/Mosaic require a separate program audit; real driver only recognizes fixed vecadd16. Read docs/program-audit.md, lock-order.md and failure-runbook.md before integration.

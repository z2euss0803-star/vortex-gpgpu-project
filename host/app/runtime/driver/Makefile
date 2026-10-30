obj-m += vxl_linux.o
# Shared core is C11 in userspace and uses C99 block declarations. Scope the
# dialect to this driver; do not change the shared Linux 5.4 build flags.
ccflags-y += -std=gnu11 -Wno-declaration-after-statement
vxl_linux-y := vxl_drv.o vxl_ioctl.o vxl_hw.o shared_range.o shared_pool.o shared_job.o shared_engine.o shared_program.o shared_cdma.o shared_cnn.o shared_hunet_sha256.o shared_hunet_layout.o shared_hunet_program.o
vxl_linux-y += vxl_s8.o vxl_s8_tracefs.o shared_s8_trace.o shared_s8_layout.o shared_s8_validate.o shared_s8_program.o shared_s8_state.o shared_s8_timing.o
vxl_linux-y += shared_s8_result.o
vxl_linux-y += vxl_profile.o shared_profile_switch.o
vxl_linux-y += vxl_mosaic.o shared_mosaic_layout.o shared_mosaic_validate.o shared_mosaic_program.o shared_mosaic_state.o shared_mosaic_runtime.o shared_mosaic_evidence.o shared_mosaic_engine.o
vxl-mosaic-source-header := $(firstword $(wildcard $(src)/vxl_mosaic_release.h $(srctree)/$(src)/vxl_mosaic_release.h))
ifneq ($(vxl-mosaic-source-header),)
ccflags-y += -DVXL_MOSAIC_SOURCE_BOUND -I$(dir $(vxl-mosaic-source-header))
endif
vxl-s8-source-header := $(firstword $(wildcard $(src)/vxl_s8_release.h $(srctree)/$(src)/vxl_s8_release.h))
ifneq ($(vxl-s8-source-header),)
ccflags-y += -DVXL_S8_SOURCE_BOUND -I$(dir $(vxl-s8-source-header))
endif
vxl_linux-y += vxl_tracefs.o
vxl_linux-y += vxl_mgmt_tracefs.o shared_mgmt_timing.o
vxl_linux-y += shared_digest.o
vxl_linux-y += vxl_digest_crypto.o
vxl_linux-y += shared_hunet_opt_load.o
vxl_linux-y += shared_hunet_opt_program.o shared_hunet_opt_validate.o
ccflags-y += -I$(src)/../include -Werror
vxl_linux-y += v2/ioctl.o v2/worker.o v2/poll.o v2/linux.o v2/program.o v2/buffers.o
vxl_linux-y += v2/control.o v2/hw.o v2/provider_s8.o v2/provider_p8.o
vxl_linux-y += v2/execution.o v2/startup.o v2/startup_linux.o
vxl_linux-y += shared_v2_owner.o shared_v2_objects.o shared_v2_validate.o shared_v2_catalog.o shared_v2_program.o shared_v2_relocate.o shared_v2_jobs.o shared_v2_completion.o shared_v2_batch.o
vxl-hunet-source-header := $(firstword $(wildcard $(src)/vxl_hunet_source.h $(srctree)/$(src)/vxl_hunet_source.h))
ifneq ($(vxl-hunet-source-header),)
ccflags-y += -DVXL_HUNET_SOURCE_BOUND -I$(dir $(vxl-hunet-source-header))
endif
vxl_linux-y += shared_hunet_fused_validate.o shared_hunet_fused_state.o shared_hunet_fused_evidence.o shared_hunet_fused_program.o
vxl-fused-source-header := $(firstword $(wildcard $(src)/vxl_fused_release.h $(srctree)/$(src)/vxl_fused_release.h))
ifneq ($(vxl-fused-source-header),)
ccflags-y += -DVXL_FUSED_SOURCE_BOUND -I$(dir $(vxl-fused-source-header))
endif
vxl_linux-y += shared_hunet_validate.o
vxl_linux-y += shared_hunet_session.o
vxl_linux-y += shared_hunet_evidence.o vxl_hunet.o
vxl_linux-y += shared_hunet_hybrid_state.o
vxl_linux-y += shared_hunet_hybrid_read.o
vxl_linux-y += shared_hunet_hybrid_write.o
vxl_linux-y += shared_hunet_hybrid_evidence.o
vxl-hybrid-source-header := $(firstword $(wildcard $(src)/vxl_hybrid_release.h $(srctree)/$(src)/vxl_hybrid_release.h))
ifneq ($(vxl-hybrid-source-header),)
ccflags-y += -DVXL_HYBRID_SOURCE_BOUND -I$(dir $(vxl-hybrid-source-header))
endif

vxl_linux-y += shared_hunet_opt_catalog.o shared_hunet_opt_state.o shared_hunet_opt_evidence.o
vxl-opt-source-header := $(firstword $(wildcard $(src)/vxl_opt_release.h $(srctree)/$(src)/vxl_opt_release.h))
ifneq ($(vxl-opt-source-header),)
ccflags-y += -DVXL_OPT_SOURCE_BOUND -I$(dir $(vxl-opt-source-header))
endif

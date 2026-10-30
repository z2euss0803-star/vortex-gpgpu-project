CC ?= gcc
CXX ?= g++
PYTHON ?= python3
OUT ?= build
CPPFLAGS += -Iinclude -I$(OUT)/generated -Ivendor/vortex/runtime/include -Ivendor/vortex/runtime/common -Ivendor/vortex/sim/common
CFLAGS += -std=c11 -O2 -g -Wall -Wextra -Werror -fPIC
CXXFLAGS += -std=c++17 -O2 -g -Wall -Wextra -Werror -fPIC
CFLAGS += -ffile-prefix-map=$(CURDIR)=/usr/src/vxl -fdebug-prefix-map=$(CURDIR)=/usr/src/vxl
CXXFLAGS += -ffile-prefix-map=$(CURDIR)=/usr/src/vxl -fdebug-prefix-map=$(CURDIR)=/usr/src/vxl
CORE = core/range.c core/pool.c core/job.c core/engine.c core/program.c core/cdma.c core/cnn.c core/hunet_sha256.c core/hunet_layout.c core/hunet_program.c core/hunet_validate.c
CORE += core/s8_layout.c core/s8_validate.c
CORE += core/s8_program.c
CORE += core/s8_state.c
CORE += core/s8_timing.c
CORE += core/s8_trace.c
CORE += core/s8_result.c
CORE += core/hunet_session.c
CORE += core/hunet_evidence.c
CORE += core/hunet_hybrid_state.c
CORE += core/hunet_hybrid_read.c
CORE += core/hunet_hybrid_write.c
CORE += core/hunet_hybrid_evidence.c
CORE += core/hunet_fused_validate.c
CORE += core/hunet_fused_state.c
CORE += core/hunet_fused_evidence.c
CORE += core/hunet_fused_program.c
CORE += core/mgmt_timing.c
CORE += core/digest.c
CORE += core/hunet_opt_load.c
CORE += core/hunet_opt_catalog.c core/hunet_opt_validate.c core/hunet_opt_program.c core/hunet_opt_state.c core/hunet_opt_evidence.c
CORE += core/profile_switch.c
CORE += core/mosaic_layout.c core/mosaic_validate.c core/mosaic_program.c core/mosaic_state.c core/mosaic_runtime.c core/mosaic_evidence.c core/mosaic_engine.c
GEN = $(OUT)/generated/VX_config.h $(OUT)/generated/VX_types.h
CNN_HEADERS = generated/cnn/programs.h include/vxl/cnn.h
export CNN_FIXTURES := $(abspath $(OUT))/fixtures
HUNET_HEADERS = $(wildcard include/vxl/hunet*.h include/uapi/vx_hunet*.h generated/hunet/*.h generated/hunet_fused/*.h generated/hunet_opt/*/*.h) include/vxl/submit_timing.h include/vxl/mgmt_timing.h include/vxl/digest.h
ifneq ($(HUNET_BINDING_HEADER),)
CPPFLAGS += -DVXL_HUNET_SOURCE_BOUND -I$(dir $(HUNET_BINDING_HEADER))
HUNET_HEADERS += $(HUNET_BINDING_HEADER)
endif
.PHONY: help test-contract test-uapi test-memory test-jobs test-probe test-copy test-submit test-runtime test-offline userspace-cross kernel-check
help:
	@echo 'test-offline test-contract test-uapi test-memory test-jobs test-probe test-copy test-submit test-runtime userspace-cross kernel-check'

.PHONY: test-opt-contract
test-opt-contract:
	$(PYTHON) -m unittest discover -s tests/hunet_opt -p test_contract.py -v
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffunction-sections -Wl,--gc-sections tests/hunet_opt/test_opt_uapi.c core/hunet_opt_validate.c core/hunet_fused_validate.c -o $(OUT)/test_opt_uapi
	./$(OUT)/test_opt_uapi

.PHONY: test-opt-mgmt-timing
test-opt-mgmt-timing:
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/hunet_opt/test_mgmt_timing.c core/mgmt_timing.c -o $(OUT)/test_opt_mgmt_timing
	./$(OUT)/test_opt_mgmt_timing

.PHONY: test-opt-cpu-timing
test-opt-cpu-timing: $(GEN)
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/hunet_opt/test_cpu_timing.c $(CORE) -o $(OUT)/test_opt_cpu_timing
	./$(OUT)/test_opt_cpu_timing

.PHONY: test-opt-load-timing
test-opt-load-timing:
	$(MAKE) $(FUSED_CLIENT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/hunet_opt/test_load_timing.cpp $(FUSED_CLIENT) -pthread -lm -Wl,--wrap=ioctl -o $(OUT)/test_opt_load_timing
	./$(OUT)/test_opt_load_timing $(OUT)/opt-load-$$(date -u +%Y%m%dT%H%M%S%N)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/hunet_opt/test_seal_timing.c $(CORE) -o $(OUT)/test_opt_seal_timing
	./$(OUT)/test_opt_seal_timing

.PHONY: test-opt-digest
test-opt-digest:
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/hunet_opt/test_digest.c core/digest.c core/hunet_sha256.c -o $(OUT)/test_opt_digest
	$(PYTHON) -m unittest discover -s tests/hunet_opt -p test_digest_vectors.py -v
	$(PYTHON) -m unittest discover -s tests/hunet_opt -p test_digest_assets.py -v

.PHONY: test-opt-digest-transactions
test-opt-digest-transactions:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/hunet_opt/test_digest_transactions.c $(CORE) -o $(OUT)/test_opt_digest_transactions
	./$(OUT)/test_opt_digest_transactions
test-hybrid-baseline:
	$(PYTHON) -m unittest discover -s tests/hunet_hybrid -p test_baseline.py
test-submit-timing:
	$(PYTHON) -m unittest discover -s tests/hunet_timing -v

$(OUT)/test_cdma_fastwait: tests/hunet_timing/test_fast_wait.c $(CORE) $(HUNET_HEADERS) driver/vxl_hw.c
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) -Itests/hunet -Itests/hunet/hw_stubs $(CFLAGS) $(CORE) $< -o $@
test-cdma-fastwait: $(OUT)/test_cdma_fastwait
	./$(OUT)/test_cdma_fastwait
test-hybrid-release:
	$(PYTHON) -m unittest discover -s tests/hunet_hybrid -p test_release.py
$(OUT)/test_hybrid_state: tests/hunet_hybrid/test_state.c tests/hunet/memory_fixture.h $(CORE) $(HUNET_HEADERS)
	mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) $< -o $@
test-hybrid-state: $(OUT)/test_hybrid_state
	./$(OUT)/test_hybrid_state
$(OUT)/test_hybrid_read: tests/hunet_hybrid/test_read.c tests/hunet_hybrid/fixture.h $(CORE) $(HUNET_HEADERS)
	mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) $< -o $@
test-hybrid-read: $(OUT)/test_hybrid_read
	./$(OUT)/test_hybrid_read
$(OUT)/test_hybrid_write: tests/hunet_hybrid/test_write.c tests/hunet_hybrid/fixture.h $(CORE) $(HUNET_HEADERS)
	mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) $< -Wl,--wrap=calloc -o $@
test-hybrid-write: $(OUT)/test_hybrid_write
	./$(OUT)/test_hybrid_write
$(OUT)/test_hybrid_evidence: tests/hunet_hybrid/test_evidence.c tests/hunet_hybrid/fixture.h $(CORE) $(HUNET_HEADERS)
	mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) $< -o $@
$(OUT)/test_hybrid_ioctl: tests/hunet_hybrid/test_ioctl.c driver/vxl_ioctl.c tests/hunet_hybrid/fixture.h $(CORE) $(HUNET_HEADERS)
	mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) -Itests/hunet/hw_stubs $(CFLAGS) $(CORE) $< -o $@
test-hybrid-evidence: $(OUT)/test_hybrid_evidence $(OUT)/test_hybrid_ioctl
	./$(OUT)/test_hybrid_evidence
	./$(OUT)/test_hybrid_ioctl
HYBRID_CLIENT = $(OUT)/userspace/hunet_client_cpu.o $(OUT)/userspace/hunet_hybrid_client.o $(OUT)/userspace/hunet_cpu_gelu.o $(OUT)/core/hunet_sha256.o $(OUT)/core/range.o $(OUT)/core/hunet_validate.o
HYBRID_TOOLS = tools/hunet_hybrid_session.cpp tools/hunet_hybrid_reference.cpp tools/hunet_timing.cpp
$(OUT)/userspace/hunet_client_cpu.o: userspace/hunet_client.c include/vxl/hunet_client.h $(HUNET_HEADERS)
	mkdir -p $(dir $@)
	$(CC) $(filter-out -DVXL_HUNET_SOURCE_BOUND,$(CPPFLAGS)) $(CFLAGS) -D_GNU_SOURCE -c $< -o $@
$(OUT)/userspace/hunet_hybrid_client.o $(OUT)/userspace/hunet_cpu_gelu.o: $(HUNET_HEADERS) include/vxl/hunet_hybrid_client.h include/vxl/hunet_cpu_gelu.h
$(OUT)/tests/hunet_hybrid/runtime_fixture.o: tests/hunet_hybrid/runtime_fixture.c tests/hunet_hybrid/fixture.h $(HUNET_HEADERS)
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(OUT)/test_hybrid_runtime: tests/hunet_hybrid/test_runtime.cpp $(HYBRID_TOOLS) tools/hunet_hybrid_session.h tools/hunet_hybrid_reference.h $(HYBRID_CLIENT) $(patsubst %.c,$(OUT)/%.o,$(CORE)) $(OUT)/tests/hunet_hybrid/runtime_fixture.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fno-fast-math -ffp-contract=off $< $(HYBRID_TOOLS) $(sort $(HYBRID_CLIENT) $(patsubst %.c,$(OUT)/%.o,$(CORE))) $(OUT)/tests/hunet_hybrid/runtime_fixture.o -pthread -lm -Wl,--wrap=open -Wl,--wrap=close -Wl,--wrap=ioctl -Wl,--wrap=hunet_cpu_gelu4 -o $@
$(OUT)/vxl_hunet_hybrid: tools/vxl_hunet_hybrid.cpp $(HYBRID_TOOLS) tools/hunet_hybrid_session.h tools/hunet_hybrid_reference.h $(HYBRID_CLIENT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fno-fast-math -ffp-contract=off $< $(HYBRID_TOOLS) $(HYBRID_CLIENT) $(LDFLAGS) -pthread -lm -ldl -o $@
$(OUT)/libvortex-hybrid.so: $(filter-out $(OUT)/userspace/hunet_client_cpu.o,$(HYBRID_CLIENT)) $(OUT)/userspace/hunet_client.o
	$(CC) -shared $^ -pthread -lm -o $@
test-hybrid-cross:
	$(PYTHON) -m unittest discover -s tests/hunet_hybrid -p test_cross.py
test-hybrid-image:
	$(PYTHON) -m unittest discover -s tests/hunet_hybrid -p test_image.py
test-hybrid-offline: test-hybrid-baseline test-hybrid-uapi test-hybrid-release test-hybrid-cpu test-hybrid-state test-hybrid-read test-hybrid-write test-hybrid-evidence test-hybrid-runtime test-hybrid-audit test-hybrid-cross
test-hybrid-sanitize:
	$(MAKE) OUT=build/hybrid-asan CFLAGS='-std=c11 -O1 -g -Wall -Wextra -Werror -fPIC -fsanitize=address,undefined -fno-omit-frame-pointer' test-hybrid-cpu test-hybrid-state test-hybrid-read test-hybrid-write test-hybrid-evidence
hybrid-userspace-cross:
	@test -n "$(CROSS_COMPILE)" || (echo CROSS_COMPILE_REQUIRED; exit 1)
	$(MAKE) -B OUT=build/aarch64 CC=$(CROSS_COMPILE)gcc CXX=$(CROSS_COMPILE)g++ LDFLAGS=-Wl,--dynamic-linker=/lib/ld-linux-aarch64.so.1 build/aarch64/libvortex-hybrid.so build/aarch64/vxl_hunet_hybrid
test-hybrid-audit:
	$(PYTHON) -m unittest discover -s tests/hunet_hybrid -p test_audit.py
	$(PYTHON) -m unittest discover -s tests/hunet_hybrid -p test_numeric.py
	$(PYTHON) -m unittest discover -s tests/hunet_hybrid -p test_timing.py
	$(PYTHON) -m unittest discover -s tests/hunet_hybrid -p test_pack_reference.py
test-hybrid-runtime: $(OUT)/test_hybrid_runtime $(OUT)/vxl_hunet_hybrid
	./$(OUT)/test_hybrid_runtime $$(mktemp -d $(OUT)/hybrid-runtime.XXXXXX)
	./$(OUT)/vxl_hunet_hybrid --help
$(OUT)/test_hybrid_cpu: tests/hunet_hybrid/test_cpu_gelu.c userspace/hunet_cpu_gelu.c include/vxl/hunet_cpu_gelu.h
	mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fno-fast-math -ffp-contract=off userspace/hunet_cpu_gelu.c $< -pthread -lm -Wl,--wrap=pthread_create -Wl,--wrap=pthread_join -o $@
test-hybrid-cpu: $(OUT)/test_hybrid_cpu
	./$(OUT)/test_hybrid_cpu ../vortex_linux_backend_hunet_v5_20261011/evidence/gelu-probes-20261012
$(OUT)/userspace/hunet_cpu_gelu.o: CFLAGS += -fno-fast-math -ffp-contract=off
$(OUT)/test_hybrid_uapi: tests/hunet_hybrid/test_uapi.c core/range.c include/uapi/vx_hunet_hybrid.h
	mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) core/range.c $< -o $@
test-hybrid-uapi: $(OUT)/test_hybrid_uapi
	./$(OUT)/test_hybrid_uapi $(OUT)/hybrid-uapi-golden.bin
	$(PYTHON) tests/hunet_hybrid/test_uapi_golden.py $(OUT)/hybrid-uapi-golden.bin
$(OUT)/generated/%.h: vendor/vortex/hw/rtl/%.vh
	mkdir -p $(OUT)/generated
	$(PYTHON) vendor/vortex/hw/scripts/gen_config.py -i $< -o $@
$(OUT)/test_core: tests/core/test_core.c $(CORE) include/vxl/core.h include/uapi/vx_linux.h $(CNN_HEADERS)
	mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) $< $(LDFLAGS) -o $@
$(OUT)/test_engine: tests/mock/test_engine.c $(CORE) mock/device_model.c $(CNN_HEADERS)
	mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) mock/device_model.c $< -o $@
test-contract:
	$(PYTHON) tests/contract/test_profiles.py
test-uapi test-memory test-jobs test-probe: $(OUT)/test_core
	./$(OUT)/test_core
$(OUT)/test_program: tests/core/test_program.c core/program.c include/vxl/core.h $(CNN_HEADERS)
	mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) core/program.c $< -o $@
$(OUT)/test_cdma: tests/mock/test_cdma.c core/cdma.c core/range.c include/vxl/core.h
	mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) core/cdma.c core/range.c $< -o $@
test-copy test-submit: $(OUT)/test_engine $(OUT)/test_program $(OUT)/test_cdma
	./$(OUT)/test_engine
	./$(OUT)/test_program
	./$(OUT)/test_cdma
$(OUT)/libvortex.so: $(GEN) vendor/vortex/runtime/stub/vortex.cpp vendor/vortex/runtime/stub/utils.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -shared vendor/vortex/runtime/stub/vortex.cpp vendor/vortex/runtime/stub/utils.cpp -pthread -ldl -o $@
$(OUT)/test_public_api: $(GEN) tests/runtime/test_public_api.cpp $(OUT)/libvortex.so
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/runtime/test_public_api.cpp -L$(OUT) -lvortex $(LDFLAGS) -o $@
test-runtime: $(OUT)/test_public_api $(OUT)/libvortex-zynq.so
	VORTEX_DRIVER=zynq VXL_TRANSPORT=mock LD_LIBRARY_PATH=$(abspath $(OUT)) ./$(OUT)/test_public_api
$(OUT)/vxl_info: tools/vxl_info.c $(OUT)/libvortex-zynq.so
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(OUT) -lvortex-zynq $(LDFLAGS) -o $@
$(OUT)/vxl_copy_test: tools/vxl_copy_test.c $(OUT)/libvortex-zynq.so
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(OUT) -lvortex-zynq $(LDFLAGS) -o $@
$(OUT)/vxl_vecadd: tools/vxl_vecadd.cpp $(OUT)/libvortex.so $(OUT)/libvortex-zynq.so
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -L$(OUT) -lvortex $(LDFLAGS) -o $@
test-tools: $(OUT)/vxl_info $(OUT)/vxl_copy_test $(OUT)/vxl_vecadd
	LD_LIBRARY_PATH=$(abspath $(OUT)) ./$(OUT)/vxl_info --mock
	LD_LIBRARY_PATH=$(abspath $(OUT)) ./$(OUT)/vxl_copy_test --mock
	LD_LIBRARY_PATH=$(abspath $(OUT)) ./$(OUT)/vxl_vecadd --mock
$(OUT)/libvortex-legacy.so: $(GEN) tests/runtime/legacy_backend.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -shared tests/runtime/legacy_backend.cpp -o $@
$(OUT)/test_legacy: $(GEN) tests/runtime/test_legacy.cpp $(OUT)/libvortex.so
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/runtime/test_legacy.cpp -L$(OUT) -lvortex -o $@
test-legacy: $(OUT)/libvortex-legacy.so $(OUT)/test_legacy
	VORTEX_DRIVER=legacy VXL_LEGACY_TRACE=$(abspath $(OUT))/legacy.trace LD_LIBRARY_PATH=$(abspath $(OUT)) ./$(OUT)/test_legacy
BACKEND_OBJS = $(patsubst %.c,$(OUT)/%.o,$(CORE) mock/device_model.c userspace/transport_ioctl.c)
$(BACKEND_OBJS): include/vxl/core.h include/vxl/engine.h include/vxl/mock.h include/vxl/transport.h include/uapi/vx_linux.h $(CNN_HEADERS)
$(BACKEND_OBJS): $(wildcard include/vxl/mosaic*.h) include/uapi/vx_mosaic.h
$(OUT)/tests/support/strict_backend.o: tests/support/strict_backend.h include/vxl/engine.h include/vxl/core.h include/uapi/vx_linux.h $(HUNET_HEADERS)
$(OUT)/%.o: %.c $(HUNET_HEADERS) Makefile
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -D_GNU_SOURCE -c $< -o $@
$(OUT)/libvortex-zynq.so: $(GEN) $(BACKEND_OBJS) runtime/zynq/vortex.cpp include/vxl/core.h include/vxl/transport.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -shared -Wl,-z,defs runtime/zynq/vortex.cpp $(BACKEND_OBJS) -pthread -o $@
test-offline: test-contract test-uapi test-memory test-jobs test-probe test-copy test-submit test-runtime
$(OUT)/test_hunet_uapi: tests/hunet/test_uapi_layout.c core/range.c include/uapi/vx_linux.h
	mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) core/range.c $< -o $@
test-hunet-uapi: $(OUT)/test_hunet_uapi
	./$(OUT)/test_hunet_uapi $(OUT)/hunet-uapi-golden.bin
	$(PYTHON) tests/hunet/test_uapi_golden.py $(OUT)
$(OUT)/test_hunet_memory: tests/hunet/test_owned_memory.c $(CORE)
	mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) $< -o $@
test-hunet-memory: $(OUT)/test_hunet_memory
	./$(OUT)/test_hunet_memory build/hunet-rv32/code.bin
$(OUT)/test_hunet_jobs: tests/hunet/test_job_validate.c $(CORE)
	mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) $< -o $@
test-hunet-jobs: $(OUT)/test_hunet_jobs
	./$(OUT)/test_hunet_jobs ../hunet_original_tcu_20261008_v1/runs/t4_pack_a2/jobs.bin
$(OUT)/test_hunet_session: tests/hunet/test_roi_session.c tests/hunet/memory_fixture.h $(CORE)
	mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) $< -o $@
test-hunet-session: $(OUT)/test_hunet_session
	./$(OUT)/test_hunet_session
$(OUT)/test_hunet_completion: tests/hunet/test_completion.c tests/hunet/memory_fixture.h $(CORE)
	mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) $< -o $@
test-hunet-completion: $(OUT)/test_hunet_completion
	./$(OUT)/test_hunet_completion
$(OUT)/test_hunet_engine: tests/hunet/test_engine_integration.c tests/hunet/memory_fixture.h $(CORE)
	mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) $< -o $@
test-hunet-engine: $(OUT)/test_hunet_engine
	./$(OUT)/test_hunet_engine
$(OUT)/test_hunet_hw_adapter: tests/hunet/test_hw_adapter.c driver/vxl_hw.c tests/hunet/memory_fixture.h $(CORE) $(HUNET_HEADERS)
	$(CC) $(CPPFLAGS) -Itests/hunet/hw_stubs $(CFLAGS) $(CORE) $< -o $@
test-hunet-hw-guard: $(OUT)/test_hunet_hw_adapter
	./$(OUT)/test_hunet_hw_adapter guard
test-hunet-hw-deadline: $(OUT)/test_hunet_hw_adapter
	./$(OUT)/test_hunet_hw_adapter deadline
$(OUT)/test_hunet_ioctl_adapter: tests/hunet/test_ioctl_adapter.c driver/vxl_ioctl.c tests/hunet/memory_fixture.h $(CORE) $(HUNET_HEADERS)
	$(CC) $(CPPFLAGS) -Itests/hunet/hw_stubs $(CFLAGS) $(CORE) $< -o $@
test-hunet-ioctl-adapter: $(OUT)/test_hunet_ioctl_adapter
	./$(OUT)/test_hunet_ioctl_adapter
$(OUT)/test_hunet_client: tests/hunet/test_client.cpp userspace/hunet_client.c $(OUT)/core/hunet_validate.o $(OUT)/core/range.o $(OUT)/core/hunet_sha256.o
	$(CC) $(CPPFLAGS) $(CFLAGS) -D_GNU_SOURCE -c userspace/hunet_client.c -o $(OUT)/hunet_client.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(OUT)/hunet_client.o $(OUT)/core/hunet_validate.o $(OUT)/core/range.o $(OUT)/core/hunet_sha256.o -Wl,--wrap=ioctl -o $@
test-hunet-client: $(OUT)/test_hunet_client $(OUT)/test_hunet_raw_session $(OUT)/libvortex-hunet.so $(OUT)/vxl_hunet $(OUT)/vxl_hunet_copy $(OUT)/vxl_hunet_wmma
	./$(OUT)/test_hunet_client
	./$(OUT)/test_hunet_raw_session
	HUNET_OUT=$(OUT) $(PYTHON) tests/hunet/test_cli.py
HUNET_CLIENT = $(OUT)/userspace/hunet_client.o $(OUT)/core/hunet_sha256.o $(OUT)/core/range.o
$(OUT)/libvortex-hunet.so: $(HUNET_CLIENT)
	$(CC) -shared -Wl,-z,defs $(HUNET_CLIENT) $(LDFLAGS) -o $@
$(OUT)/test_hunet_raw_session: tests/hunet/test_session.cpp tools/hunet_session.cpp tools/hunet_timing.cpp $(HUNET_CLIENT) $(OUT)/core/hunet_validate.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< tools/hunet_session.cpp tools/hunet_timing.cpp $(HUNET_CLIENT) $(OUT)/core/hunet_validate.o -Wl,--wrap=ioctl -Wl,--wrap=clock_gettime -o $@
$(OUT)/vxl_hunet: tools/vxl_hunet.cpp tools/hunet_session.cpp tools/hunet_timing.cpp $(HUNET_CLIENT) $(OUT)/core/hunet_validate.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tools/vxl_hunet.cpp tools/hunet_session.cpp tools/hunet_timing.cpp $(HUNET_CLIENT) $(OUT)/core/hunet_validate.o $(LDFLAGS) -o $@
$(OUT)/vxl_hunet_copy: tools/vxl_hunet_copy.cpp tools/hunet_timing.cpp $(HUNET_CLIENT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tools/vxl_hunet_copy.cpp tools/hunet_timing.cpp $(HUNET_CLIENT) $(LDFLAGS) -o $@
$(OUT)/vxl_hunet_wmma: tools/vxl_hunet_wmma.cpp tools/hunet_timing.cpp $(HUNET_CLIENT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tools/vxl_hunet_wmma.cpp tools/hunet_timing.cpp $(HUNET_CLIENT) $(LDFLAGS) -o $@
test-hunet-auditors:
	$(PYTHON) -m unittest discover -s tests/hunet -p test_numeric_evidence.py -v
	$(PYTHON) -m unittest discover -s tests/hunet -p test_timing.py -v
	$(PYTHON) -m unittest discover -s tests/hunet -p test_cross_audit.py -v
	$(PYTHON) -m unittest discover -s tests/hunet -p test_image_audit.py -v
	$(PYTHON) -m unittest discover -s tests/hunet -p test_prepare_image.py -v
	$(PYTHON) -m unittest discover -s tests/hunet -p test_actual_image_layout.py -v
	$(PYTHON) -m unittest discover -s tests/hunet -p test_kernel_warning.py -v
$(BACKEND_OBJS) $(OUT)/test_core $(OUT)/test_engine $(OUT)/test_hunet_memory $(OUT)/test_hunet_jobs $(OUT)/test_hunet_session $(OUT)/test_hunet_completion $(OUT)/test_hunet_engine $(OUT)/userspace/hunet_client.o: $(HUNET_HEADERS)
test-hunet-offline: test-hunet-uapi test-hunet-memory test-hunet-jobs test-hunet-session test-hunet-completion test-hunet-engine test-hunet-client test-hunet-hw-guard test-hunet-hw-deadline test-hunet-ioctl-adapter
	$(PYTHON) -m unittest discover -s tests/hunet -p test_profiles.py -v
	$(PYTHON) -m unittest discover -s tests/hunet -p test_crt_audit.py -v
	$(PYTHON) -m unittest discover -s tests/hunet -p test_release.py -v
	$(PYTHON) -m unittest discover -s tests/hunet -p test_build_dependencies.py -v
test-hunet-sanitize:
	$(MAKE) OUT=build/hunet-asan CFLAGS='-std=c11 -O1 -g -Wall -Wextra -Werror -fPIC -fsanitize=address,undefined -fno-omit-frame-pointer' test-hunet-memory test-hunet-jobs test-hunet-session test-hunet-completion test-hunet-engine test-hunet-hw-guard test-hunet-hw-deadline test-hunet-ioctl-adapter
userspace-cross:
	@test -n "$(CROSS_COMPILE)" || (echo CROSS_COMPILE_REQUIRED; exit 1)
	$(MAKE) -B OUT=build/aarch64 CC=$(CROSS_COMPILE)gcc CXX=$(CROSS_COMPILE)g++ LDFLAGS=-Wl,--dynamic-linker=/lib/ld-linux-aarch64.so.1 build/aarch64/libvortex.so build/aarch64/libvortex-zynq.so build/aarch64/test_public_api build/aarch64/test_core build/aarch64/vxl_info build/aarch64/vxl_copy_test build/aarch64/vxl_vecadd build/aarch64/vxl_cnn build/aarch64/test_cnn_identity build/aarch64/libvortex-hunet.so build/aarch64/vxl_hunet build/aarch64/vxl_hunet_copy build/aarch64/vxl_hunet_wmma
kernel-check:
	@test -n "$(KERNEL_SOURCE)" -a -n "$(KERNEL_BUILD)" -a -n "$(CROSS_COMPILE)"
	$(MAKE) -C $(KERNEL_SOURCE) O=$(KERNEL_BUILD) ARCH=arm64 CROSS_COMPILE=$(CROSS_COMPILE) M=$(CURDIR)/driver W=1 modules

test-hunet-cross-audit:
	$(PYTHON) -m unittest discover -s tests/hunet -p test_cross_audit.py -v

.PHONY: test-cnn-reference
$(OUT)/test_cnn_reference: tools/cnn_reference.cpp tests/cnn/test_reference.cpp tools/cnn_reference.h include/vxl/cnn.h
	mkdir -p $(OUT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tools/cnn_reference.cpp tests/cnn/test_reference.cpp -o $@
test-cnn-reference: $(OUT)/test_cnn_reference
	$(PYTHON) tests/cnn/test_reference.py
	$(PYTHON) scripts/cnn/generate_fixtures.py --out $(OUT)/fixtures
	./$(OUT)/test_cnn_reference $(OUT)/fixtures

.PHONY: test-cnn-kernels
test-cnn-kernels: test-cnn-reference
	$(PYTHON) tests/cnn/test_kernels.py

$(OUT)/test_cnn_program: tests/core/test_cnn_program.c $(CORE) generated/cnn/programs.h include/vxl/cnn.h
	mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) $< -o $@
test-cnn-contract: $(OUT)/test_cnn_program $(OUT)/test_program
	./$(OUT)/test_cnn_program
	./$(OUT)/test_program

$(OUT)/test_cnn_preflight: tests/mock/test_cnn_preflight.c tests/support/strict_backend.c $(CORE) mock/device_model.c $(CNN_HEADERS)
	mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) mock/device_model.c tests/support/strict_backend.c $< -o $@
test-cnn-preflight: $(OUT)/test_cnn_preflight
	./$(OUT)/test_cnn_preflight

$(OUT)/test_cnn_timing: tools/cnn_timing.cpp tools/cnn_timing.h tests/cnn/test_timing.cpp
	mkdir -p $(OUT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tools/cnn_timing.cpp tests/cnn/test_timing.cpp -o $@
test-cnn-timing: $(OUT)/test_cnn_timing
	./$(OUT)/test_cnn_timing
	$(PYTHON) tests/cnn/test_summary.py

$(OUT)/test_cnn_session: tools/cnn_session.cpp tools/cnn_session.h tools/cnn_reference.cpp tools/cnn_reference.h tools/cnn_timing.cpp tools/cnn_timing.h tests/cnn/test_session.cpp $(BACKEND_OBJS) $(OUT)/tests/support/strict_backend.o $(CNN_HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tools/cnn_session.cpp tools/cnn_reference.cpp tools/cnn_timing.cpp tests/cnn/test_session.cpp $(BACKEND_OBJS) $(OUT)/tests/support/strict_backend.o -o $@
$(OUT)/test_cnn_close: tests/cnn/test_close.c $(BACKEND_OBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(BACKEND_OBJS) -Wl,--wrap=close -o $@
$(OUT)/test_cnn_identity: tests/cnn/test_identity.cpp tools/cnn_identity.cpp tools/cnn_identity.h
	mkdir -p $(OUT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/cnn/test_identity.cpp tools/cnn_identity.cpp $(LDFLAGS) -o $@
test-cnn-identity: $(OUT)/test_cnn_identity
	./$(OUT)/test_cnn_identity
$(OUT)/vxl_cnn: tools/vxl_cnn.cpp tools/cnn_identity.cpp tools/cnn_identity.h tools/cnn_session.cpp tools/cnn_session.h tools/cnn_timing.cpp tools/cnn_timing.h tools/cnn_reference.cpp tools/cnn_reference.h $(BACKEND_OBJS) $(CNN_HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(if $(SOURCE_COMMIT),-DVXL_CNN_SOURCE_COMMIT=\"$(SOURCE_COMMIT)\") tools/vxl_cnn.cpp tools/cnn_identity.cpp tools/cnn_session.cpp tools/cnn_timing.cpp tools/cnn_reference.cpp $(BACKEND_OBJS) $(LDFLAGS) -o $@
test-cnn-session: test-cnn-identity $(OUT)/test_cnn_session $(OUT)/test_cnn_close $(OUT)/vxl_cnn
	./$(OUT)/test_cnn_session $(OUT)/session-evidence
	./$(OUT)/test_cnn_close
	CNN_OUT=$(OUT) $(PYTHON) tests/cnn/test_cli.py
	CNN_OUT=$(OUT) $(PYTHON) tests/cnn/test_session_evidence.py

.PHONY: test-cnn-contract test-cnn-preflight test-cnn-timing test-cnn-session test-cnn-release test-cnn-offline
test-cnn-release:
	$(PYTHON) tests/cnn/test_release.py
	$(PYTHON) tests/cnn/test_evidence.py
test-cnn-offline: test-offline test-cnn-reference test-cnn-kernels test-cnn-contract test-cnn-preflight test-cnn-timing test-cnn-session test-cnn-release

# File/runner checks are offline evidence; do not invoke run_board.sh or a GPU.
.PHONY: test-cnn-auditors
test-cnn-auditors: test-cnn-session
	$(PYTHON) -m unittest discover -s tests/cnn -p test_image_audit.py
	$(PYTHON) -m unittest discover -s tests/cnn -p 'test_*rtl*.py'
	$(PYTHON) -m unittest discover -s tests/cnn -p 'test_review_*.py'
	CNN_OUT=$(OUT) $(PYTHON) -m unittest discover -s tests/cnn -p test_board_receipt.py
	$(PYTHON) -m unittest discover -s tests/cnn -p test_board_runner.py
test-fused-catalog:
	python3 -m unittest discover -s tests/hunet_fused -p test_catalog.py -v
test-fused-uapi:
	$(PYTHON) -m unittest discover -s tests/hunet_fused -p test_uapi.py -v
test-fused-layout:
	$(PYTHON) -m unittest discover -s tests/hunet_fused -p 'test_layout*.py' -v

test-fused-math:
	$(PYTHON) -m unittest discover -s tests/hunet_fused -p test_math.py -v

fused-rv32:
	bash scripts/hunet_fused/build_rv32.sh
	$(PYTHON) -m unittest discover -s tests/hunet_fused -p test_rv32.py -v

test-fused-rtl-receipt:
	$(PYTHON) -m unittest discover -s tests/hunet_fused -p test_rtl_receipt.py -v

test-fused-rtl:
	@test -n "$(FUSED_ELF)" -a -n "$(RTL_ROOT)" -a -n "$(RTL_OUT)"
	$(PYTHON) scripts/hunet_fused/rtl_check.py --elf "$(FUSED_ELF)" --rtl-root "$(RTL_ROOT)" --out "$(RTL_OUT)"

$(OUT)/test_fused_validate: tests/hunet_fused/test_validate.c $(CORE) $(HUNET_HEADERS)
	mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) $< -o $@
$(OUT)/test_fused_program: tests/hunet_fused/test_program.c $(CORE) $(HUNET_HEADERS)
	mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) $< -o $@
test-fused-validate: $(OUT)/test_fused_validate $(OUT)/test_fused_program
	./$(OUT)/test_fused_validate
	./$(OUT)/test_fused_program

$(OUT)/test_fused_state: tests/hunet_fused/test_state.c tests/hunet_hybrid/fixture.h $(CORE) $(HUNET_HEADERS)
	mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) $< -o $@
test-fused-state: $(OUT)/test_fused_state
	./$(OUT)/test_fused_state

$(OUT)/test_fused_seal: tests/hunet_fused/test_seal.c tests/hunet/memory_fixture.h $(CORE) $(HUNET_HEADERS)
	mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) $< -o $@
test-fused-seal: $(OUT)/test_fused_seal
	./$(OUT)/test_fused_seal

$(OUT)/test_fused_adapter: tests/hunet_fused/test_adapter.c $(CORE) $(HUNET_HEADERS) driver/vxl_hw.c
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) -Itests/hunet -Itests/hunet/hw_stubs $(CFLAGS) $(CORE) $< -o $@
$(OUT)/test_fused_ioctl: tests/hunet_fused/test_ioctl.c $(CORE) $(HUNET_HEADERS) driver/vxl_ioctl.c
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) -Itests/hunet/hw_stubs $(CFLAGS) $(CORE) $< -o $@
test-fused-adapter: $(OUT)/test_fused_adapter $(OUT)/test_fused_ioctl
	./$(OUT)/test_fused_adapter
	./$(OUT)/test_fused_ioctl

test-fused-runtime:
	$(MAKE) $(OUT)/test_fused_runtime $(OUT)/vxl_hunet_fused
	./$(OUT)/test_fused_runtime $(OUT)/fused-runtime-$$(date -u +%Y%m%dT%H%M%S%N)
	FUSED_OUT=$(OUT) $(PYTHON) tests/hunet_fused/test_cli.py

FUSED_CLIENT = $(OUT)/userspace/hunet_client_cpu.o $(OUT)/userspace/hunet_fused_client.o $(OUT)/userspace/hunet_cpu_gelu.o $(OUT)/core/hunet_sha256.o $(OUT)/core/range.o $(OUT)/core/hunet_validate.o $(OUT)/core/hunet_fused_validate.o
FUSED_TOOLS = tools/hunet_fused_session.cpp tools/hunet_hybrid_reference.cpp tools/hunet_timing.cpp
$(OUT)/userspace/hunet_fused_client.o: userspace/hunet_fused_client.c include/vxl/hunet_fused_client.h tools/hunet_opt_timing.h $(HUNET_HEADERS)
$(OUT)/tests/hunet_fused/runtime_fixture.o: tests/hunet_fused/runtime_fixture.c tests/hunet_hybrid/fixture.h $(HUNET_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(OUT)/test_fused_runtime: tests/hunet_fused/test_runtime.cpp $(FUSED_TOOLS) tools/hunet_fused_session.h tools/hunet_fused_timing.h $(FUSED_CLIENT) $(patsubst %.c,$(OUT)/%.o,$(CORE)) $(OUT)/tests/hunet_fused/runtime_fixture.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fno-fast-math -ffp-contract=off $< $(FUSED_TOOLS) $(sort $(FUSED_CLIENT) $(patsubst %.c,$(OUT)/%.o,$(CORE))) $(OUT)/tests/hunet_fused/runtime_fixture.o -pthread -lm -Wl,--wrap=open -Wl,--wrap=close -Wl,--wrap=ioctl -Wl,--wrap=hunet_cpu_gelu4 -o $@
$(OUT)/vxl_hunet_fused: tools/vxl_hunet_fused.cpp $(FUSED_TOOLS) tools/hunet_fused_session.h tools/hunet_fused_timing.h $(FUSED_CLIENT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fno-fast-math -ffp-contract=off $< $(FUSED_TOOLS) $(FUSED_CLIENT) $(LDFLAGS) -pthread -lm -ldl -o $@
$(OUT)/libvortex-fused.so: $(FUSED_CLIENT)
	$(CC) -shared $^ -pthread -lm -o $@

test-fused-audit: test-fused-runtime
	$(PYTHON) -m unittest discover -s tests/hunet_fused -p test_audit.py -v
	$(PYTHON) -m unittest discover -s tests/hunet_fused -p test_summary.py -v

test-fused-release:
	$(PYTHON) -m unittest discover -s tests/hunet_fused -p test_release.py -v
test-fused-cross:
	$(PYTHON) -m unittest discover -s tests/hunet_fused -p test_cross.py -v
test-fused-image:
	$(PYTHON) -m unittest discover -s tests/hunet_fused -p test_image.py -v

fused-userspace-cross:
	@test -n "$(CROSS_COMPILE)" || (echo CROSS_COMPILE_REQUIRED; exit 1)
	$(MAKE) -B OUT=build/aarch64 CC=$(CROSS_COMPILE)gcc CXX=$(CROSS_COMPILE)g++ LDFLAGS=-Wl,--dynamic-linker=/lib/ld-linux-aarch64.so.1 build/aarch64/libvortex-fused.so build/aarch64/vxl_hunet_fused
fused-release:
	@test -n "$(RV32)" -a -n "$(RELEASE)" -a -n "$(CPU_DIR)" || (echo RV32_RELEASE_CPU_DIR_REQUIRED; exit 1)
	$(PYTHON) scripts/hunet_fused/package_release.py --root . --rv32 "$(RV32)" --cpu-dir "$(CPU_DIR)" --out "$(RELEASE)"
fused-cross:
	@test -n "$(RELEASE)" -a -n "$(TOOLCHAIN)" -a -n "$(KERNEL)" -a -n "$(CROSS_OUT)" || (echo EXPLICIT_CROSS_PATHS_REQUIRED; exit 1)
	$(PYTHON) scripts/hunet_fused/cross.py kernel --release "$(RELEASE)" --toolchain "$(TOOLCHAIN)" --kernel "$(KERNEL)" --kernel-build "$(KERNEL_BUILD)" --out "$(CROSS_OUT)"
test-fused-offline: test-fused-catalog test-fused-uapi test-fused-layout test-fused-math test-fused-rtl-receipt test-fused-validate test-fused-state test-fused-seal test-fused-adapter test-fused-audit test-fused-release test-fused-cross

$(OUT)/test_cdma_management: tests/hunet_timing/test_management_wait.c driver/vxl_ioctl.c $(CORE) $(HUNET_HEADERS)
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) -Itests/hunet/hw_stubs $(CFLAGS) $(CORE) $< -o $@
test-cdma-management: $(OUT)/test_cdma_management
	./$(OUT)/test_cdma_management
test-cdma-fastwait: test-cdma-management

# Isolate the ISA extension from baseline dispatch and all portable code.
$(OUT)/userspace/digest_sha2_block.o: userspace/digest_sha2_block.c userspace/digest_sha2_block.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -march=armv8-a+crypto -c $< -o $@
$(OUT)/test_digest_dispatch: tests/hunet_opt/test_digest_dispatch.c tests/hunet_opt/sha2_emulation.c tests/hunet_opt/sha2_emulation.h userspace/digest_sha2.c userspace/digest_sha2_block.h userspace/digest_dispatch.c core/digest.c core/hunet_sha256.c include/vxl/digest.h
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/hunet_opt/test_digest_dispatch.c tests/hunet_opt/sha2_emulation.c userspace/digest_sha2.c userspace/digest_dispatch.c core/digest.c core/hunet_sha256.c -Wl,--wrap=calloc -o $@
test-opt-user-sha2: $(OUT)/test_digest_dispatch
	./$(OUT)/test_digest_dispatch
	$(PYTHON) tests/hunet_opt/test_digest_sha2_build.py

$(OUT)/test_kernel_digest: tests/hunet_opt/test_kernel_digest.c driver/vxl_digest_crypto.c core/digest.c core/hunet_sha256.c include/vxl/digest.h Makefile
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) -Itests/hunet_opt/crypto_stubs $(CFLAGS) -Wmissing-prototypes tests/hunet_opt/test_kernel_digest.c driver/vxl_digest_crypto.c core/digest.c core/hunet_sha256.c -o $@
test-opt-kernel-digest: $(OUT)/test_kernel_digest
	./$(OUT)/test_kernel_digest
	$(PYTHON) tests/hunet_opt/test_kernel_crypto_config.py

$(OUT)/test_opt_load_fastwait: tests/hunet_opt/test_load_fastwait.c $(CORE) $(HUNET_HEADERS)
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) $< -o $@
test-opt-load-fastwait: $(OUT)/test_opt_load_fastwait test-cdma-fastwait
	./$(OUT)/test_opt_load_fastwait

$(OUT)/test_opt_gn_math: tests/hunet_opt/test_gn_math.cpp device/hunet_opt_gn.h
	@mkdir -p $(OUT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fno-fast-math -ffp-contract=off $< -lm -o $@
test-opt-gn-math: $(OUT)/test_opt_gn_math
	./$(OUT)/test_opt_gn_math
	$(PYTHON) tests/hunet_opt/test_gn_layout.py
	$(PYTHON) tests/hunet_opt/test_gn_captures.py

$(OUT)/test_opt_gn_phases: tests/hunet_opt/test_gn_phases.cpp device/hunet_opt_gn_runtime.h device/hunet_opt_gn.h
	@mkdir -p $(OUT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fno-fast-math -ffp-contract=off $< -pthread -lm -o $@
test-opt-gn-phases: $(OUT)/test_opt_gn_phases
	./$(OUT)/test_opt_gn_phases
	$(PYTHON) tests/hunet_opt/test_device_source.py
	OPT_CXX="$(CXX)" $(PYTHON) tests/hunet_opt/test_gn_mutations.py

$(OUT)/test_opt_pack: tests/hunet_opt/test_pack.cpp device/hunet_opt_pack.h device/hunet_fused_ops.h
	@mkdir -p $(OUT)
	$(CXX) $(CPPFLAGS) -Ivendor/hunet/v5/include $(CXXFLAGS) -fno-fast-math -ffp-contract=off $< -o $@
$(OUT)/test_opt_lookup: tests/hunet_opt/test_descriptor_lookup.cpp device/hunet_opt_pack.h
	@mkdir -p $(OUT)
	$(CXX) $(CPPFLAGS) -Ivendor/hunet/v5/include $(CXXFLAGS) $< -o $@
test-opt-pack: $(OUT)/test_opt_pack $(OUT)/test_opt_lookup
	./$(OUT)/test_opt_lookup
	./$(OUT)/test_opt_pack

$(OUT)/test_opt_wmma: tests/hunet_opt/test_wmma_tiles.cpp device/hunet_opt_wmma.h device/hunet_opt_pack.h
	@mkdir -p $(OUT)
	$(CXX) $(CPPFLAGS) -Ivendor/hunet/v5/include $(CXXFLAGS) -fno-fast-math -ffp-contract=off $< -o $@
test-opt-wmma: $(OUT)/test_opt_wmma
	./$(OUT)/test_opt_wmma
	$(PYTHON) tests/hunet_opt/test_gn_layout.py
	$(PYTHON) tests/hunet_opt/test_device_source.py

opt-generate:
	$(PYTHON) -m scripts.hunet_opt.generate --candidate G1 --out generated/hunet_opt/G1
$(OUT)/test_opt_validate: tests/hunet_opt/test_opt_validate.c $(CORE) $(HUNET_HEADERS) generated/hunet_opt/G1/catalog.h
	@mkdir -p $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) $< -o $@
test-opt-graph: $(OUT)/test_opt_validate
	./$(OUT)/test_opt_validate
	$(PYTHON) -m unittest discover -s tests/hunet_opt -p test_graph_catalog.py -v
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tests/hunet_opt/test_opt_restore.c -o $(OUT)/test_opt_restore
	./$(OUT)/test_opt_restore

$(OUT)/test_opt_gn_norm: tests/hunet_opt/test_gn_norm.cpp tests/hunet_opt/test_gn_phases.cpp device/hunet_opt_gn_norm.h device/hunet_opt_gn_runtime.h
	@mkdir -p $(OUT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fno-fast-math -ffp-contract=off $< -pthread -lm -o $@
test-opt-gn-fusion: $(OUT)/test_opt_gn_norm
	./$(OUT)/test_opt_gn_norm
	$(PYTHON) -m unittest discover -s tests/hunet_opt -p test_gn_fusion.py -v
	$(CC) $(CPPFLAGS) $(CFLAGS) -DVXL_OPT_CATALOG_HEADER=\"../generated/hunet_opt/G3_NORM/catalog.h\" $(CORE) tests/hunet_opt/test_opt_validate.c -o $(OUT)/test_opt_norm_validate
	./$(OUT)/test_opt_norm_validate
	OPT_CXX="$(CXX)" $(PYTHON) tests/hunet_opt/test_gn_norm_mutations.py

$(OUT)/test_opt_views: tests/hunet_opt/test_views.cpp device/hunet_opt_views.h device/hunet_opt_pack.h
	@mkdir -p $(OUT)
	$(CXX) $(CPPFLAGS) -Ivendor/hunet/v5/include $(CXXFLAGS) -fno-fast-math -ffp-contract=off $< -o $@
test-opt-views: $(OUT)/test_opt_views
	./$(OUT)/test_opt_views
	$(PYTHON) -m unittest discover -s tests/hunet_opt -p test_view_liveness.py -v
	$(CC) $(CPPFLAGS) $(CFLAGS) -DVXL_OPT_CATALOG_HEADER=\"../generated/hunet_opt/G3_VIEWS/catalog.h\" $(CORE) tests/hunet_opt/test_opt_validate.c -o $(OUT)/test_opt_views_validate
	./$(OUT)/test_opt_views_validate

$(OUT)/test_opt_scalar: tests/hunet_opt/test_scalar.cpp device/hunet_opt_scalar.h
	@mkdir -p $(OUT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fno-fast-math -ffp-contract=off $< -lm -o $@
test-opt-scalar: $(OUT)/test_opt_scalar
	./$(OUT)/test_opt_scalar
	$(PYTHON) -m unittest discover -s tests/hunet_opt -p test_op_coverage.py -v

.PHONY: test-opt-runtime-state test-opt-runtime
# Compile the same state/CPU transaction core against each selected immutable catalog.
test-opt-runtime-state: $(GEN)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tests/hunet_opt/test_opt_state.c -o $(OUT)/test_opt_state
	./$(OUT)/test_opt_state
	$(CC) $(CPPFLAGS) $(CFLAGS) -DVXL_OPT_CATALOG_HEADER=\"../generated/hunet_opt/G3_NORM/catalog.h\" $(CORE) tests/hunet_opt/test_opt_state.c -o $(OUT)/test_opt_norm_state
	./$(OUT)/test_opt_norm_state
	$(CC) $(CPPFLAGS) $(CFLAGS) -DVXL_OPT_CATALOG_HEADER=\"../generated/hunet_opt/G3_VIEWS/catalog.h\" $(CORE) tests/hunet_opt/test_opt_state.c -o $(OUT)/test_opt_views_state
	./$(OUT)/test_opt_views_state

OPT_CANDIDATE ?= G1
OPT_CRYPTO ?= 0
OPT_USER_CRYPTO = $(OUT)/userspace/digest_dispatch.o
ifeq ($(OPT_CRYPTO),1)
OPT_USER_CRYPTO += $(OUT)/userspace/digest_sha2.o $(OUT)/userspace/digest_sha2_block.o
else
OPT_USER_CRYPTO += $(OUT)/userspace/digest_host_only.o
endif
OPT_CLIENT = $(OUT)/userspace/hunet_client_cpu.o $(OUT)/userspace/hunet_opt_client.o $(OUT)/userspace/hunet_cpu_gelu.o $(patsubst %.c,$(OUT)/%.o,$(CORE)) $(OPT_USER_CRYPTO)
OPT_TOOLS = tools/hunet_opt_session.cpp tools/hunet_hybrid_reference.cpp tools/hunet_timing.cpp
$(OUT)/userspace/hunet_opt_client.o: $(HUNET_HEADERS) tools/hunet_opt_timing.h
$(OUT)/test_opt_runtime: tests/hunet_opt/test_runtime.cpp $(OPT_TOOLS) tools/hunet_opt_session.h tools/hunet_opt_timing_json.h $(OPT_CLIENT) $(OUT)/tests/hunet_opt/runtime_fixture.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fno-fast-math -ffp-contract=off $< $(OPT_TOOLS) $(OPT_CLIENT) $(OUT)/tests/hunet_opt/runtime_fixture.o -pthread -lm -Wl,--wrap=open -Wl,--wrap=close -Wl,--wrap=ioctl -Wl,--wrap=hunet_cpu_gelu4 -o $@
$(OUT)/libvortex-opt.so: $(OPT_CLIENT)
	$(CXX) -shared $(OPT_CLIENT) -pthread -lm -o $@
test-opt-runtime: test-opt-runtime-state $(OUT)/test_opt_runtime
	./$(OUT)/test_opt_runtime $(OUT)/opt-runtime-$$(date -u +%Y%m%dT%H%M%S%N)

$(OUT)/vxl_hunet_opt: tools/vxl_hunet_opt.cpp $(OPT_TOOLS) tools/hunet_opt_session.h tools/hunet_opt_timing_json.h $(OPT_CLIENT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DVXL_OPT_CANDIDATE=\"$(OPT_CANDIDATE)\" -fno-fast-math -ffp-contract=off $< $(OPT_TOOLS) $(OPT_CLIENT) $(LDFLAGS) -pthread -lm -ldl -o $@

test-opt-cpu-transactions: $(GEN)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tests/hunet_opt/test_cpu_read.c -o $(OUT)/test_opt_cpu_read
	./$(OUT)/test_opt_cpu_read
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tests/hunet_opt/test_cpu_write.c -Wl,--wrap=calloc -o $(OUT)/test_opt_cpu_write
	./$(OUT)/test_opt_cpu_write

$(patsubst %.c,$(OUT)/%.o,$(CORE)): $(HUNET_HEADERS) Makefile

.PHONY: test-opt-runtime-live test-opt-runtime-variants test-opt-owner
test-opt-runtime-live: $(GEN) $(OUT)/test_opt_runtime $(OUT)/vxl_hunet_opt
	./$(OUT)/test_opt_runtime $(OUT)/live-$$(date -u +%Y%m%dT%H%M%S%N)
	./$(OUT)/vxl_hunet_opt
	./$(OUT)/vxl_hunet_opt --help
test-opt-runtime-variants:
	$(MAKE) OUT=$(OUT)/opt-norm CPPFLAGS='$(CPPFLAGS) -DVXL_OPT_CATALOG_HEADER=\"../generated/hunet_opt/G3_NORM/catalog.h\"' OPT_CANDIDATE=G3_NORM test-opt-runtime-live
	$(MAKE) OUT=$(OUT)/opt-views CPPFLAGS='$(CPPFLAGS) -DVXL_OPT_CATALOG_HEADER=\"../generated/hunet_opt/G3_VIEWS/catalog.h\"' OPT_CANDIDATE=G3_VIEWS test-opt-runtime-live
test-opt-owner: $(GEN)
	@for candidate in G1 G2_WMMA2 G2_WMMA4 G3_VIEWS; do \
	 $(PYTHON) tests/hunet_opt/owner_oracle.py $$candidate $(OUT)/owner-$$candidate.bin && \
	 $(CC) $(CPPFLAGS) $(CFLAGS) -DVXL_OPT_CATALOG_HEADER=\"../generated/hunet_opt/$$candidate/catalog.h\" $(CORE) tests/hunet_opt/test_owner.c -o $(OUT)/test_owner_$$candidate && \
	 ./$(OUT)/test_owner_$$candidate $(OUT)/owner-$$candidate.bin || exit $$?; \
	 done

test-opt-client-load: $(OPT_CLIENT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/hunet_opt/test_client_load.cpp $(OPT_CLIENT) -pthread -lm -Wl,--wrap=ioctl -o $(OUT)/test_opt_client_load
	./$(OUT)/test_opt_client_load $(OUT)/opt-client-load-$$(date -u +%Y%m%dT%H%M%S%N)

.PHONY: test-opt-numeric
test-opt-numeric:
	$(PYTHON) -m unittest discover -s tests/hunet_opt -p test_numeric_policy.py -v
	$(PYTHON) -m unittest discover -s tests/hunet_opt -p test_reference_graph.py -v

$(OUT)/opt_reference_engine: tests/hunet_opt/reference_engine.cpp $(OUT)/core/hunet_validate.o $(OUT)/core/range.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fno-fast-math -ffp-contract=off $< $(OUT)/core/hunet_validate.o $(OUT)/core/range.o -lm -o $@
test-opt-reference-engine: $(OUT)/opt_reference_engine
	./$(OUT)/opt_reference_engine --selftest

test-opt-propagation: test-opt-reference-engine
	$(PYTHON) tests/hunet_opt/run_propagation.py

.PHONY: test-opt-audit
.PHONY: test-opt-release

.PHONY: test-opt-image
test-opt-image:
	$(PYTHON) -m unittest discover -s tests/hunet_opt -p test_image.py -v
test-opt-release:
	$(PYTHON) -m unittest discover -s tests/hunet_opt -p test_release.py -v

.PHONY: test-opt-cross
test-opt-cross:
	$(PYTHON) -m unittest discover -s tests/hunet_opt -p test_guest_gate.py -v
	$(PYTHON) -m unittest discover -s tests/hunet_opt -p test_cross_audit.py -v

test-opt-audit:
	$(PYTHON) -m unittest discover -s tests/hunet_opt -p test_timing_audit.py -v
	$(PYTHON) -m unittest discover -s tests/hunet_opt -p test_candidate_matrix.py -v

.PHONY: test-opt-combined test-opt-offline
test-opt-combined:
	$(MAKE) OUT=$(OUT)/opt-combined CPPFLAGS='$(CPPFLAGS) -DVXL_OPT_CATALOG_HEADER=\"../generated/hunet_opt/COMBINED/catalog.h\"' OPT_CANDIDATE=COMBINED test-opt-runtime-live
# Software-only suite. No RTL/Vivado, image build, device or media command.
test-opt-offline: test-opt-contract test-opt-mgmt-timing test-opt-cpu-timing test-opt-load-timing test-opt-digest test-opt-digest-transactions test-opt-user-sha2 test-opt-kernel-digest test-opt-load-fastwait test-opt-gn-math test-opt-gn-phases test-opt-pack test-opt-wmma test-opt-graph test-opt-gn-fusion test-opt-views test-opt-scalar test-opt-runtime test-opt-runtime-variants test-opt-combined test-opt-owner test-opt-client-load test-opt-cpu-transactions test-opt-numeric test-opt-propagation test-opt-audit test-fused-uapi test-fused-layout test-fused-math test-fused-validate test-fused-state test-fused-seal test-fused-adapter test-fused-runtime test-hybrid-uapi test-hybrid-cpu test-hybrid-state test-hybrid-read test-hybrid-write test-hybrid-evidence test-hybrid-runtime test-hunet-uapi test-hunet-memory test-hunet-jobs test-hunet-session test-hunet-completion test-hunet-engine test-hunet-hw-guard test-hunet-ioctl-adapter
	@echo HOST_OPT_OFFLINE_SUITE_COMPLETE_NOT_BOARD

include Makefile.s8

include Makefile.demo

include Makefile.mosaic

include Makefile.nnrt

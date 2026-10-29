#include <stdint.h>
#include <stddef.h>
#include "host_platform.h"
#include "gpgpu_hw.h"
#include "vecadd16.h"

#define POLL_LIMIT 100000000u
#define GPU_POLL_LIMIT 10000000u
#define GPU_WATCHDOG_CYCLES 100000000u
#define GUARD_WORDS 16u
#define C_REGION_BYTES 384u

static void barrier(void) { host_barrier(); }

static uint32_t rd(uint64_t base, uint32_t offset) {
    return host_mmio_read(base + offset);
}

static void wr(uint64_t base, uint32_t offset, uint32_t value) {
    host_mmio_write(base + offset, value);
    barrier();
}


static void flush(uint64_t address, uint32_t bytes) {
    host_flush(address, bytes);
    barrier();
}

static void invalidate(uint64_t address, uint32_t bytes) {
    host_invalidate(address, bytes);
    barrier();
}

static void print_address(const char *label, uint64_t address) {
    host_log("%s %08x_%08x\r\n", label,
               (uint32_t)(address >> 32), (uint32_t)address);
}

static void snapshot(const char *where) {
    host_log("SNAPSHOT %s gpu_status=%08x errors=%08x wr=%08x rd=%08x elapsed=%08x jobs=%08x ready=%08x gpio=%08x cdma_sr=%08x\r\n",
               where, rd(GPU_BASE, GPU_STATUS), rd(GPU_BASE, GPU_ERRORS),
               rd(GPU_BASE, GPU_PENDING_WRITES), rd(GPU_BASE, GPU_PENDING_READS),
               rd(GPU_BASE, GPU_ELAPSED), rd(GPU_BASE, GPU_JOBS),
               rd(GPU_BASE, GPU_MEMORY_READY), rd(READY_GPIO_BASE, 0),
               rd(CDMA_BASE, CDMA_SR));
}

static int fail(const char *where, uint32_t index, uint32_t expected, uint32_t actual) {
    host_log("FAIL %s index=%u expected=%08x actual=%08x\r\n",
               where, index, expected, actual);
    snapshot(where);
    host_log("Preserve UART and registers before reset or reprogramming.\r\n");
    return -1;
}

static int ps_range(uint64_t address, uint32_t bytes) {
    return bytes != 0 && address >= PS_CODE &&
           address + bytes <= UINT64_C(0x30000000) && address + bytes > address;
}

static int pl_range(uint64_t address, uint32_t bytes) {
    return bytes != 0 && address >= PL_BASE &&
           address + bytes <= PL_BASE + UINT64_C(0x100000000) &&
           address + bytes > address;
}

/* CDMA has no DRE: both endpoints and BTT must be 16-byte aligned. */
int host_cdma_copy(uint64_t source, uint64_t dest, uint32_t bytes) {
    uint32_t status = 0;
    uint32_t i;
    if ((source & 15u) || (dest & 15u) || (bytes & 15u) ||
        bytes == 0 || bytes > 4096u ||
        !((ps_range(source, bytes) || pl_range(source, bytes)) &&
          (ps_range(dest, bytes) || pl_range(dest, bytes))))
        return fail("CDMA_ARGUMENT", bytes, 0, 1);
    for (i = 0; i < POLL_LIMIT; ++i) {
        status = rd(CDMA_BASE, CDMA_SR);
        if (cdma_status_error(status)) return fail("CDMA_PRIOR_ERROR", i, 0, status);
        if (status & CDMA_SR_IDLE) break;
    }
    if (i == POLL_LIMIT) return fail("CDMA_PRIOR_TIMEOUT", i, CDMA_SR_IDLE, status);
    wr(CDMA_BASE, CDMA_CR, 0u);
    /* SR interrupt bits are write-one-to-clear. An old IOC must not satisfy
     * the completion condition for the new BTT. Errors remain untouched. */
    wr(CDMA_BASE, CDMA_SR, CDMA_SR_W1C_NORMAL);
    status = rd(CDMA_BASE, CDMA_SR);
    if (cdma_status_error(status)) return fail("CDMA_CLEAR_ERROR", 0, 0, status);
    if (status & CDMA_SR_W1C_NORMAL)
        return fail("CDMA_STALE_IOC", 0, 0, status);
    wr(CDMA_BASE, CDMA_SA_MSB, (uint32_t)(source >> 32));
    wr(CDMA_BASE, CDMA_SA, (uint32_t)source);
    wr(CDMA_BASE, CDMA_DA_MSB, (uint32_t)(dest >> 32));
    wr(CDMA_BASE, CDMA_DA, (uint32_t)dest);
    barrier();
    wr(CDMA_BASE, CDMA_BTT, bytes);
    for (i = 0; i < POLL_LIMIT; ++i) {
        status = rd(CDMA_BASE, CDMA_SR);
        if (cdma_status_error(status)) return fail("CDMA_ERROR", i, 0, status);
        if (cdma_status_complete(status)) return 0;
    }
    return fail("CDMA_TIMEOUT", i, CDMA_SR_IDLE | CDMA_SR_IOC, status);
}

static uint32_t input_a(uint32_t round, uint32_t i) {
    return UINT32_C(0x12340000) + round * 101u + i * 7u;
}

static uint32_t input_b(uint32_t round, uint32_t i) {
    return UINT32_C(0x80000100) - round * 13u - i * 3u;
}

static void prepare_words(uint64_t address, uint32_t round, int which) {
    uint64_t p = address;
    uint32_t i;
    for (i = 0; i < VEC_COUNT; ++i)
        host_ps_write(p + 4u*(i), which == 0 ? input_a(round, i) : input_b(round, i));
    flush(address, VEC_COUNT * sizeof(uint32_t));
}

static void prepare_output(uint64_t address) {
    uint64_t p = address;
    uint32_t i;
    for (i = 0; i < GUARD_WORDS; ++i) host_ps_write(p + 4u*(i), UINT32_C(0x55aa55aa));
    for (i = 0; i < VEC_COUNT; ++i) host_ps_write(p + 4u*(GUARD_WORDS + i), UINT32_C(0xdeadbeef));
    for (i = 0; i < GUARD_WORDS; ++i)
        host_ps_write(p + 4u*(GUARD_WORDS + VEC_COUNT + i), UINT32_C(0xaa55aa55));
    flush(address, C_REGION_BYTES);
}

int host_check_output(uint64_t address, uint32_t round) {
    uint64_t p;
    uint32_t i, expected;
    invalidate(address, C_REGION_BYTES);
    p = address;
    for (i = 0; i < GUARD_WORDS; ++i) {
        if (host_ps_read(p + 4u*(i)) != UINT32_C(0x55aa55aa))
            return fail("LEFT_GUARD", i, UINT32_C(0x55aa55aa), host_ps_read(p + 4u*(i)));
        if (host_ps_read(p + 4u*(GUARD_WORDS + VEC_COUNT + i)) != UINT32_C(0xaa55aa55))
            return fail("RIGHT_GUARD", i, UINT32_C(0xaa55aa55),
                        host_ps_read(p + 4u*(GUARD_WORDS + VEC_COUNT + i)));
    }
    for (i = 0; i < VEC_COUNT; ++i) {
        expected = input_a(round, i) + input_b(round, i);
        if (host_ps_read(p + 4u*(GUARD_WORDS + i)) != expected)
            return fail("GPU_GOLDEN", i, expected, host_ps_read(p + 4u*(GUARD_WORDS + i)));
    }
    return 0;
}

static int wait_ready(void) {
    uint32_t i, ctrl, gpio;
    for (i = 0; i < POLL_LIMIT; ++i) {
        ctrl = rd(GPU_BASE, GPU_MEMORY_READY) & 1u;
        gpio = rd(READY_GPIO_BASE, 0) & 1u;
        if (ctrl && gpio) return 0;
    }
    return fail("DDR_CALIBRATION_TIMEOUT", i, 1,
                rd(GPU_BASE, GPU_MEMORY_READY) & 1u);
}

int host_run_gpu(uint32_t round, uint32_t a, uint32_t b, uint32_t c) {
    uint32_t before, status = 0, errors, i;
    if (rd(GPU_BASE, GPU_STATUS) != 0u)
        return fail("GPU_NOT_IDLE", round, 0, rd(GPU_BASE, GPU_STATUS));
    before = rd(GPU_BASE, GPU_JOBS);
    uint64_t args = PS_ARGS;
    /* The GPU reads full cache lines, including the argument padding. */
    for (i = 0; i < CACHE_LINE / 4u; ++i) host_ps_write(args + 4u*(i), UINT32_C(0xc0de0000) + i);
    host_ps_write(args + 4u*(0), a);
    host_ps_write(args + 4u*(1), b);
    host_ps_write(args + 4u*(2), c);
    flush(PS_ARGS, CACHE_LINE);
    wr(GPU_BASE, GPU_PC, (uint32_t)PS_CODE);
    wr(GPU_BASE, GPU_ARG, (uint32_t)PS_ARGS);
    wr(GPU_BASE, GPU_WATCHDOG, GPU_WATCHDOG_CYCLES);
    wr(GPU_BASE, GPU_TEX_MODE, 0u);
    barrier();
    wr(GPU_BASE, GPU_COMMAND, 1u);
    for (i = 0; i < GPU_POLL_LIMIT; ++i) {
        status = rd(GPU_BASE, GPU_STATUS);
        if (status & GPU_STATUS_ERROR ||
            ((status >> GPU_STATUS_STATE_SHIFT) & 7u) == GPU_STATE_FAULT)
            return fail("GPU_FAULT", round, 0, rd(GPU_BASE, GPU_ERRORS));
        if (status & GPU_STATUS_DONE) break;
    }
    if (i == GPU_POLL_LIMIT) return fail("GPU_HOST_TIMEOUT", round, GPU_STATUS_DONE, status);
    errors = rd(GPU_BASE, GPU_ERRORS);
    if (errors) return fail("GPU_ERRORS_AT_DONE", round, 0, errors);
    if (rd(GPU_BASE, GPU_PENDING_WRITES) || rd(GPU_BASE, GPU_PENDING_READS))
        return fail("GPU_PENDING_AT_DONE", round, 0,
                    rd(GPU_BASE, GPU_PENDING_WRITES) | rd(GPU_BASE, GPU_PENDING_READS));
    if (rd(GPU_BASE, GPU_JOBS) != before + 1u)
        return fail("GPU_JOB_COUNT", round, before + 1u, rd(GPU_BASE, GPU_JOBS));
    invalidate(PS_FLUSH_LINE, CACHE_LINE);
    for (i = 0; i < CACHE_LINE / 4u; ++i) {
        uint32_t expected = UINT32_C(0xf1a50000) + i;
        if (host_ps_read(PS_FLUSH_LINE + 4u*(i)) != expected)
            return fail("GPU_FLUSH_LINE_MODIFIED", i, expected, host_ps_read(PS_FLUSH_LINE + 4u*(i)));
    }
    host_log("GPU_DONE round=%u elapsed=%u jobs=%u\r\n", round,
               rd(GPU_BASE, GPU_ELAPSED), rd(GPU_BASE, GPU_JOBS));
    return 0;
}

static uint32_t pl_pattern(uint64_t address, uint32_t i) {
    return UINT32_C(0xa5a50000) ^ (uint32_t)(address >> 4) ^
           ((uint32_t)(address >> 32) * UINT32_C(0x9e3779b9)) ^
           (i * UINT32_C(0x00010203));
}

static void stage_pattern(uint64_t address, uint32_t bytes) {
    uint64_t src = PS_STAGE;
    uint32_t i;
    for (i = 0; i < bytes / 4u; ++i) host_ps_write(src + 4u*(i), pl_pattern(address, i));
    flush(PS_STAGE, bytes);
}

static int verify_pl_pattern(uint64_t address, uint32_t bytes, const char *where) {
    uint64_t dst = PS_STAGE2;
    uint32_t i, expected;
    /* Poison and flush the whole receive window before each DMA read. An
     * old PS cache or DDR value cannot produce a false roundtrip success. */
    for (i = 0; i < bytes / 4u; ++i) host_ps_write(dst + 4u*(i), ~pl_pattern(address, i));
    flush(PS_STAGE2, bytes);
    invalidate(PS_STAGE2, bytes);
    if (host_cdma_copy(address, PS_STAGE2, bytes)) return -1;
    invalidate(PS_STAGE2, bytes);
    for (i = 0; i < bytes / 4u; ++i) {
        expected = pl_pattern(address, i);
        if (host_ps_read(dst + 4u*(i)) != expected) {
            print_address(where, address);
            return fail(where, i, expected, host_ps_read(dst + 4u*(i)));
        }
    }
    return 0;
}

static int roundtrip_high(uint64_t destination, uint32_t bytes) {
    /* Clear bit 31 of the PL offset. If the 40-bit path truncates that bit,
     * a high-bank write will overwrite this low-bank sentinel. */
    const uint64_t low_alias = destination - UINT64_C(0x80000000);
    stage_pattern(low_alias, bytes);
    if (host_cdma_copy(PS_STAGE, low_alias, bytes)) return -1;
    stage_pattern(destination, bytes);
    if (host_cdma_copy(PS_STAGE, destination, bytes)) return -1;
    if (verify_pl_pattern(destination, bytes, "PL_HIGH_DATA")) return -1;
    if (verify_pl_pattern(low_alias, bytes, "PL_LOW_ALIAS_SENTINEL")) return -1;
    print_address("PL_ROUNDTRIP_PASS", destination);
    return 0;
}

static int partial_copy(void) {
    const uint64_t pl = PL_BASE + UINT64_C(0x30000);
    uint64_t src = PS_STAGE;
    uint64_t dst = PS_STAGE2;
    uint32_t i, expected;
    for (i = 0; i < 16u; ++i) host_ps_write(src + 4u*(i), UINT32_C(0x11110000) + i);
    flush(PS_STAGE, CACHE_LINE);
    if (host_cdma_copy(PS_STAGE, pl, CACHE_LINE)) return -1;
    for (i = 0; i < 4u; ++i) host_ps_write(src + 4u*(16u + i), UINT32_C(0xcafe0000) + i);
    flush(PS_STAGE + CACHE_LINE, CACHE_LINE);
    if (host_cdma_copy(PS_STAGE + CACHE_LINE, pl + 16u, 16u)) return -1;
    invalidate(PS_STAGE2, CACHE_LINE);
    if (host_cdma_copy(pl, PS_STAGE2, CACHE_LINE)) return -1;
    invalidate(PS_STAGE2, CACHE_LINE);
    for (i = 0; i < 16u; ++i) {
        expected = i >= 4u && i < 8u ? UINT32_C(0xcafe0000) + i - 4u
                                         : UINT32_C(0x11110000) + i;
        if (host_ps_read(dst + 4u*(i)) != expected) return fail("CDMA_PARTIAL_GUARD", i, expected, host_ps_read(dst + 4u*(i)));
    }
    host_log("CDMA_PARTIAL_PASS 16 bytes inside 64-byte line\r\n");
    return 0;
}

static int vector_round(uint32_t round, uint32_t a_pl, uint32_t b_pl) {
    const uint64_t pl_a = PL_BASE + UINT64_C(0x20000);
    const uint64_t pl_b = PL_BASE + UINT64_C(0x20100);
    const uint64_t pl_c_guard = PL_BASE + UINT64_C(0x20200);
    uint32_t a = a_pl ? PL_GPU_A : PS_GPU_A;
    uint32_t b = b_pl ? PL_GPU_B : PS_GPU_B;
    uint32_t c = PL_GPU_C;
    if (a_pl) {
        prepare_words(PS_STAGE, round, 0);
        if (host_cdma_copy(PS_STAGE, pl_a, VEC_COUNT * 4u)) return -1;
    } else prepare_words(PS_GPU_A, round, 0);
    if (b_pl) {
        prepare_words(PS_STAGE, round, 1);
        if (host_cdma_copy(PS_STAGE, pl_b, VEC_COUNT * 4u)) return -1;
    } else prepare_words(PS_GPU_B, round, 1);
    prepare_output(PS_STAGE);
    if (host_cdma_copy(PS_STAGE, pl_c_guard, C_REGION_BYTES)) return -1;
    if (host_run_gpu(round, a, b, c)) return -1;
    invalidate(PS_STAGE2, C_REGION_BYTES);
    if (host_cdma_copy(pl_c_guard, PS_STAGE2, C_REGION_BYTES)) return -1;
    if (host_check_output(PS_STAGE2, round)) return -1;
    host_log("VECADD64_PASS round=%u A=%s B=%s C=PL\r\n", round,
               a_pl ? "PL" : "PS", b_pl ? "PL" : "PS");
    wr(GPU_BASE, GPU_COMMAND, 2u);
    if (rd(GPU_BASE, GPU_STATUS) != 0u)
        return fail("GPU_ACK", round, 0, rd(GPU_BASE, GPU_STATUS));
    return 0;
}


/* Dedicated PS buffers leave a complete cache line on both sides of each
 * transfer. CPU cache ownership changes only after DMA completion. */
#define LOOP_TX UINT64_C(0x10040040)
#define LOOP_RX UINT64_C(0x10042040)
static uint32_t loop_word(uint32_t seed, uint32_t i) {
    return UINT32_C(0xa5a50000) ^ (seed * UINT32_C(0x9e3779b9)) ^
           (i * UINT32_C(0x00010203));
}
static int loop_copy(uint64_t pl, uint32_t bytes, uint32_t seed) {
    uint64_t src = LOOP_TX;
    uint64_t dst = LOOP_RX;
    uint64_t left = LOOP_RX - CACHE_LINE;
    uint64_t right = LOOP_RX + bytes;
    uint32_t i, expected;
    host_log("LOOP_COPY_BEGIN seed=%u bytes=%u pl=%08x_%08x\r\n",
               seed, bytes, (uint32_t)(pl >> 32), (uint32_t)pl);
    for (i = 0; i < bytes / 4u; ++i) {
        host_ps_write(src + 4u*(i), loop_word(seed, i));
        host_ps_write(dst + 4u*(i), ~host_ps_read(src + 4u*(i)));
    }
    for (i = 0; i < 16u; ++i) {
        host_ps_write(left + 4u*(i), UINT32_C(0x55aa55aa));
        host_ps_write(right + 4u*(i), UINT32_C(0xaa55aa55));
    }
    flush(LOOP_TX, bytes);
    flush(LOOP_RX - CACHE_LINE, bytes + 2u * CACHE_LINE);
    if (host_cdma_copy(LOOP_TX, pl, bytes)) return -1;
    if (host_cdma_copy(pl, LOOP_RX, bytes)) return -1;
    invalidate(LOOP_RX - CACHE_LINE, bytes + 2u * CACHE_LINE);
    for (i = 0; i < bytes / 4u; ++i) {
        expected = loop_word(seed, i);
        if (host_ps_read(dst + 4u*(i)) != expected) return fail("LOOP_COPY_DATA", i, expected, host_ps_read(dst + 4u*(i)));
    }
    for (i = 0; i < 16u; ++i) {
        if (host_ps_read(left + 4u*(i)) != UINT32_C(0x55aa55aa))
            return fail("LOOP_LEFT_GUARD", i, UINT32_C(0x55aa55aa), host_ps_read(left + 4u*(i)));
        if (host_ps_read(right + 4u*(i)) != UINT32_C(0xaa55aa55))
            return fail("LOOP_RIGHT_GUARD", i, UINT32_C(0xaa55aa55), host_ps_read(right + 4u*(i)));
    }
    host_log("LOOP_COPY_PASS seed=%u bytes=%u\r\n", seed, bytes);
    return 0;
}
int host_main(void) {
    uint64_t code = PS_CODE;
    uint32_t id, config, capability, i;
    host_phase(1);
    host_log("PS_PL_LOOPBACK_HOST_BEGIN build=20261001\r\n");
    host_log("STEP01 GPU_ID_READ_BEGIN addr=A0000000\r\n");
    id = rd(GPU_BASE, GPU_ID);
    host_log("STEP01 GPU_ID_READ_END id=%08x\r\n", id);
    if (id != UINT32_C(0x56580204)) return fail("GPU_ID", 0, UINT32_C(0x56580204), id);
    config = rd(GPU_BASE, GPU_CONFIG);
    capability = rd(GPU_BASE, GPU_CAPABILITY);
    if (config != UINT32_C(0x00040404)) return fail("GPU_CONFIG", 0, UINT32_C(0x00040404), config);
    if (capability != UINT32_C(0x00010001)) return fail("GPU_CAPABILITY", 0, UINT32_C(0x00010001), capability);
    host_log("STEP02 CONTROL_PASS config=%08x capability=%08x\r\n", config, capability);
    host_log("STEP03 DDR_READY_WAIT_BEGIN\r\n");
    if (wait_ready()) return -1;
    host_log("STEP03 DDR_READY_PASS\r\n");
    host_log("STEP04 CDMA_STATUS_READ_BEGIN\r\n");
    host_log("STEP04 CDMA_STATUS=%08x\r\n", rd(CDMA_BASE, CDMA_SR));
    host_phase(3);
    if (loop_copy(PL_BASE + UINT64_C(0x40000), 16u, 1u)) return -1;
    if (loop_copy(PL_BASE + UINT64_C(0x40000), 64u, 2u)) return -1;
    if (loop_copy(PL_BASE + UINT64_C(0x40000), 256u, 3u)) return -1;
    if (loop_copy(PL_BASE + UINT64_C(0x40000), 4096u, 4u)) return -1;
    if (loop_copy(PL_BASE + UINT64_C(0x50ff0), 256u, 5u)) return -1;
    if (roundtrip_high(PL_HIGH_BANK, 256u)) return -1;
    if (roundtrip_high(PL_LAST_LINE, 64u)) return -1;
    if (partial_copy()) return -1;
    host_log("STEP05 CDMA_MEMORY_LOOPBACK_PASS\r\n");
    for (i = 0; i < 128u / 4u; ++i) host_ps_write(code + 4u*(i), UINT32_C(0x00100073));
    for (i = 0; i < sizeof(vecadd16_code) / sizeof(vecadd16_code[0]); ++i) host_ps_write(code + 4u*(i), vecadd16_code[i]);
    flush(PS_CODE, 128u);
    for (i = 0; i < CACHE_LINE / 4u; ++i)
        host_ps_write(PS_FLUSH_LINE + 4u*(i), UINT32_C(0xf1a50000) + i);
    flush(PS_FLUSH_LINE, CACHE_LINE);
    host_phase(4);
    host_log("STEP06 GPU_LOOPBACK_BEGIN\r\n");
    if (vector_round(0u, 1u, 0u)) return -1;
    if (vector_round(1u, 0u, 1u)) return -1;
    if (vector_round(2u, 1u, 1u)) return -1;
    host_phase(5);
    if (loop_copy(PL_BASE + UINT64_C(0x40000), 64u, 8u)) return -1;
    snapshot("LOOPBACK_FINAL");
    host_log("PS_PL_LOOPBACK_HOST_PASS 3x64 sums; DMA sizes/high/end/partial/guards/repeat\r\n");
    return 0;
}

// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

`ifndef VX_CONFIG_VH
`define VX_CONFIG_VH

`ifndef MIN
`define MIN(x, y)   (((x) < (y)) ? (x) : (y))
`endif

`ifndef MAX
`define MAX(x, y)   (((x) > (y)) ? (x) : (y))
`endif

`ifndef CLAMP
`define CLAMP(x, lo, hi)   (((x) > (hi)) ? (hi) : (((x) < (lo)) ? (lo) : (x)))
`endif

`ifndef UP
`define UP(x)   (((x) != 0) ? (x) : 1)
`endif

///////////////////////////////////////////////////////////////////////////////

`ifndef EXT_M_DISABLE
`define EXT_M_ENABLE
`endif

`ifndef EXT_F_DISABLE
`define EXT_F_ENABLE
`endif

`ifdef XLEN_64
`ifndef FPU_DSP
`ifndef EXT_D_DISABLE
`define EXT_D_ENABLE
`endif
`endif
`endif

`ifndef EXT_ZICOND_DISABLE
`define EXT_ZICOND_ENABLE
`endif

`ifndef XLEN_32
`ifndef XLEN_64
`define XLEN_32
`endif
`endif

`ifdef XLEN_64
`define XLEN 64
`endif

`ifdef XLEN_32
`define XLEN 32
`endif

`ifdef EXT_D_ENABLE
`define FLEN_64
`else
`define FLEN_32
`endif

`ifdef FLEN_64
`define FLEN 64
`endif

`ifdef FLEN_32
`define FLEN 32
`endif

`ifdef XLEN_64
`ifdef FLEN_32
    `define FPU_RV64F
`endif
`endif

`ifndef VLEN
`define VLEN (4 * `XLEN)
`endif

`ifndef NUM_CLUSTERS
`define NUM_CLUSTERS 1
`endif

`ifndef NUM_CORES
`define NUM_CORES 1
`endif

`ifndef NUM_WARPS
`define NUM_WARPS 4
`endif

`ifndef NUM_THREADS
`define NUM_THREADS 4
`endif

`ifndef NUM_BARRIERS
`define NUM_BARRIERS `UP(`NUM_WARPS/2)
`endif

`ifndef SOCKET_SIZE
`define SOCKET_SIZE `MIN(4, `NUM_CORES)
`endif

`ifdef L1_DISABLE
    `define ICACHE_DISABLE
    `define DCACHE_DISABLE
`endif

`ifndef MEM_BLOCK_SIZE
`define MEM_BLOCK_SIZE 64
`endif

`ifndef MEM_ADDR_WIDTH
`ifdef XLEN_64
`define MEM_ADDR_WIDTH 48
`else
`define MEM_ADDR_WIDTH 32
`endif
`endif

`ifndef L1_LINE_SIZE
`define L1_LINE_SIZE `MEM_BLOCK_SIZE
`endif

`ifndef L2_LINE_SIZE
`define L2_LINE_SIZE `MEM_BLOCK_SIZE
`endif

`ifndef L3_LINE_SIZE
`define L3_LINE_SIZE `MEM_BLOCK_SIZE
`endif

// 平台存储器参数

`ifndef PLATFORM_MEMORY_NUM_BANKS
`define PLATFORM_MEMORY_NUM_BANKS 2
`endif

`ifndef PLATFORM_MEMORY_ADDR_WIDTH
`ifdef XLEN_64
    `define PLATFORM_MEMORY_ADDR_WIDTH 48
`else
    `define PLATFORM_MEMORY_ADDR_WIDTH 32
`endif
`endif

`ifndef PLATFORM_MEMORY_DATA_SIZE
`define PLATFORM_MEMORY_DATA_SIZE 64
`endif

`ifndef PLATFORM_MEMORY_INTERLEAVE
`define PLATFORM_MEMORY_INTERLEAVE 1
`endif

`ifdef XLEN_64

`ifndef STACK_BASE_ADDR
`define STACK_BASE_ADDR 64'h1FFFF0000
`endif

`ifndef STARTUP_ADDR
`define STARTUP_ADDR    64'h080000000
`endif

`ifndef USER_BASE_ADDR
`define USER_BASE_ADDR  64'h000010000
`endif

`ifndef IO_BASE_ADDR
`define IO_BASE_ADDR    64'h000000040
`endif

`ifdef VM_ENABLE
`ifndef PAGE_TABLE_BASE_ADDR
`define PAGE_TABLE_BASE_ADDR 64'h0F0000000
`endif

`endif

`else // XLEN_32

`ifndef STACK_BASE_ADDR
`define STACK_BASE_ADDR 32'hFFFF0000
`endif

`ifndef STARTUP_ADDR
`define STARTUP_ADDR    32'h80000000
`endif

`ifndef USER_BASE_ADDR
`define USER_BASE_ADDR  32'h00010000
`endif

`ifndef IO_BASE_ADDR
`define IO_BASE_ADDR    32'h00000040
`endif

`ifdef VM_ENABLE
`ifndef PAGE_TABLE_BASE_ADDR
`define PAGE_TABLE_BASE_ADDR 32'hF0000000
`endif

`endif

`endif

`define IO_END_ADDR     `USER_BASE_ADDR

`ifndef LMEM_LOG_SIZE
`define LMEM_LOG_SIZE   14
`endif

`ifndef LMEM_BASE_ADDR
`define LMEM_BASE_ADDR  `STACK_BASE_ADDR
`endif

`ifndef IO_COUT_ADDR
`define IO_COUT_ADDR    `IO_BASE_ADDR
`endif
`define IO_COUT_SIZE    64

`ifndef IO_MPM_ADDR
`define IO_MPM_ADDR     (`IO_COUT_ADDR + `IO_COUT_SIZE)
`endif

`ifndef STACK_LOG2_SIZE
`define STACK_LOG2_SIZE 13
`endif

`define RESET_DELAY     8

`ifndef STALL_TIMEOUT
`define STALL_TIMEOUT   (100000 * (1 ** (`L2_ENABLED + `L3_ENABLED)))
`endif

`ifndef SV_DPI
`ifndef DPI_DISABLE
`define DPI_DISABLE
`endif
`endif

`ifndef FPU_FPNEW
`ifndef FPU_DSP
`ifndef FPU_DPI
`ifndef SYNTHESIS
`ifndef DPI_DISABLE
`define FPU_DPI
`else
`define FPU_DSP
`endif
`else
`define FPU_DSP
`endif
`endif
`endif
`endif

`ifndef SYNTHESIS
`ifndef DPI_DISABLE
`define IMUL_DPI
`define IDIV_DPI
`endif
`endif

`ifndef DEBUG_LEVEL
`define DEBUG_LEVEL 3
`endif

`ifndef MEM_PAGE_SIZE
`define MEM_PAGE_SIZE (4096)
`endif

`ifndef MEM_PAGE_LOG2_SIZE
`define MEM_PAGE_LOG2_SIZE (12)
`endif

// 虚拟存储配置 ///////////////////////////////////////////////
`ifdef VM_ENABLE
    `ifdef XLEN_32
        `ifndef VM_ADDR_MODE
        `define VM_ADDR_MODE SV32  //或采用 BARE 模式
        `endif
        `ifndef PT_LEVEL
        `define PT_LEVEL (2)
        `endif
        `ifndef PTE_SIZE
        `define PTE_SIZE (4)
        `endif
        `ifndef NUM_PTE_ENTRY
        `define NUM_PTE_ENTRY (1024)
        `endif
        `ifndef PT_SIZE_LIMIT
        `define PT_SIZE_LIMIT (1<<23)
        `endif
    `else
        `ifndef VM_ADDR_MODE
        `define VM_ADDR_MODE SV39 //或采用 BARE 模式
        `endif
        `ifndef PT_LEVEL
        `define PT_LEVEL (3)
        `endif
        `ifndef PTE_SIZE
        `define PTE_SIZE (8)
        `endif
        `ifndef NUM_PTE_ENTRY
        `define NUM_PTE_ENTRY (512)
        `endif
        `ifndef PT_SIZE_LIMIT
        `define PT_SIZE_LIMIT (1<<25)
        `endif
    `endif

    `ifndef PT_SIZE
    `define PT_SIZE MEM_PAGE_SIZE
    `endif

    `ifndef TLB_SIZE
    `define TLB_SIZE (32)
    `endif

`endif

// 流水线配置 /////////////////////////////////////////////////////

`ifndef SIMD_WIDTH
`define SIMD_WIDTH      `NUM_THREADS
`endif

// 发射宽度
`ifndef ISSUE_WIDTH
`define ISSUE_WIDTH     `UP(`NUM_WARPS / 16)
`endif

// 操作数收集器
`ifndef NUM_OPCS
`define NUM_OPCS        `UP(`NUM_WARPS / (4 * `ISSUE_WIDTH))
`endif

// 寄存器堆分组数
`ifndef NUM_GPR_BANKS
`define NUM_GPR_BANKS   4
`endif
`ifndef NUM_VGPR_BANKS
`define NUM_VGPR_BANKS  2
`endif

// ALU 单元数量
`ifndef NUM_ALU_LANES
`define NUM_ALU_LANES   `SIMD_WIDTH
`endif
`ifndef NUM_ALU_BLOCKS
`define NUM_ALU_BLOCKS  `ISSUE_WIDTH
`endif

// FPU 单元数量
`ifndef NUM_FPU_LANES
`define NUM_FPU_LANES   `SIMD_WIDTH
`endif
`ifndef NUM_FPU_BLOCKS
`define NUM_FPU_BLOCKS  `ISSUE_WIDTH
`endif

// LSU 单元数量
`ifndef NUM_LSU_LANES
`define NUM_LSU_LANES   `SIMD_WIDTH
`endif
`ifndef NUM_LSU_BLOCKS
`define NUM_LSU_BLOCKS  1
`endif

// SFU 单元数量
`ifndef NUM_SFU_LANES
`define NUM_SFU_LANES   `SIMD_WIDTH
`endif
`define NUM_SFU_BLOCKS  1

// VPU 单元数量
`ifndef NUM_VPU_LANES
`define NUM_VPU_LANES   `SIMD_WIDTH
`endif
`ifndef NUM_VPU_BLOCKS
`define NUM_VPU_BLOCKS  `ISSUE_WIDTH
`endif

// TCU 单元数量
`define NUM_TCU_LANES   `NUM_THREADS
`ifndef NUM_TCU_BLOCKS
`define NUM_TCU_BLOCKS  `ISSUE_WIDTH
`endif

// 指令缓冲区容量
`ifndef IBUF_SIZE
`define IBUF_SIZE   4
`endif

// LSU 行大小
`ifndef LSU_LINE_SIZE
`define LSU_LINE_SIZE   `MIN(`NUM_LSU_LANES * (`XLEN / 8), `L1_LINE_SIZE)
`endif

// LSU 核心请求队列容量
`ifndef LSUQ_IN_SIZE
`define LSUQ_IN_SIZE    (2 * (`SIMD_WIDTH / `NUM_LSU_LANES))
`endif

// LSU 存储器请求队列容量
`ifndef LSUQ_OUT_SIZE
`define LSUQ_OUT_SIZE   `MAX(`LSUQ_IN_SIZE, `LSU_LINE_SIZE / (`XLEN / 8))
`endif

// 浮点运算单元 ///////////////////////////////////////////////////////

// FPU 请求队列容量
`ifndef FPUQ_SIZE
`define FPUQ_SIZE (2 * (`SIMD_WIDTH / `NUM_FPU_LANES))
`endif

// FNCP 延迟
`ifndef LATENCY_FNCP
`define LATENCY_FNCP 2
`endif

// FMA 延迟
`ifndef LATENCY_FMA
`ifdef FPU_DPI
`define LATENCY_FMA 4
`endif
`ifdef FPU_FPNEW
`define LATENCY_FMA 4
`endif
`ifdef FPU_DSP
`ifdef QUARTUS
`define LATENCY_FMA 4
`endif
`ifdef VIVADO
`define LATENCY_FMA 16
`endif
`ifndef LATENCY_FMA
`define LATENCY_FMA 4
`endif
`endif
`endif

// FDIV 延迟
`ifndef LATENCY_FDIV
`ifdef FPU_DPI
`define LATENCY_FDIV 15
`endif
`ifdef FPU_FPNEW
`define LATENCY_FDIV 16
`endif
`ifdef FPU_DSP
`ifdef QUARTUS
`define LATENCY_FDIV 15
`endif
`ifdef VIVADO
`define LATENCY_FDIV 28
`endif
`ifndef LATENCY_FDIV
`define LATENCY_FDIV 16
`endif
`endif
`endif

// FSQRT 延迟
`ifndef LATENCY_FSQRT
`ifdef FPU_DPI
`define LATENCY_FSQRT 10
`endif
`ifdef FPU_FPNEW
`define LATENCY_FSQRT 16
`endif
`ifdef FPU_DSP
`ifdef QUARTUS
`define LATENCY_FSQRT 10
`endif
`ifdef VIVADO
`define LATENCY_FSQRT 28
`endif
`ifndef LATENCY_FSQRT
`define LATENCY_FSQRT 16
`endif
`endif
`endif

// FCVT 延迟
`ifndef LATENCY_FCVT
`define LATENCY_FCVT 5
`endif

// FMA 带宽比例
`ifndef FMA_PE_RATIO
`define FMA_PE_RATIO 1
`endif

// FDIV 带宽比例
`ifndef FDIV_PE_RATIO
`define FDIV_PE_RATIO 8
`endif

// FSQRT 带宽比例
`ifndef FSQRT_PE_RATIO
`define FSQRT_PE_RATIO 8
`endif

// FCVT 带宽比例
`ifndef FCVT_PE_RATIO
`define FCVT_PE_RATIO 8
`endif

// FNCP 带宽比例
`ifndef FNCP_PE_RATIO
`define FNCP_PE_RATIO 2
`endif

// 指令缓存可配置参数 //////////////////////////////////////////////////

// 启用缓存
`ifndef ICACHE_DISABLE
`define ICACHE_ENABLE
`endif

`ifndef ICACHE_ENABLE
    `define NUM_ICACHES 0
`endif

// 缓存单元数量
`ifndef NUM_ICACHES
`define NUM_ICACHES `UP(`SOCKET_SIZE / 4)
`endif

// 缓存容量
`ifndef ICACHE_SIZE
`define ICACHE_SIZE 16384
`endif

// 核心响应队列容量
`ifndef ICACHE_CRSQ_SIZE
`define ICACHE_CRSQ_SIZE 2
`endif

// 缺失处理寄存器容量
`ifndef ICACHE_MSHR_SIZE
`define ICACHE_MSHR_SIZE 16
`endif

// 存储器请求队列容量
`ifndef ICACHE_MREQ_SIZE
`define ICACHE_MREQ_SIZE 4
`endif

// 存储器响应队列容量
`ifndef ICACHE_MRSQ_SIZE
`define ICACHE_MRSQ_SIZE 0
`endif

// 组相联路数
`ifndef ICACHE_NUM_WAYS
`define ICACHE_NUM_WAYS 4
`endif

// 替换策略
`ifndef ICACHE_REPL_POLICY
`define ICACHE_REPL_POLICY 1
`endif

`ifndef ICACHE_MEM_PORTS
`define ICACHE_MEM_PORTS 1
`endif

// 数据缓存可配置参数 //////////////////////////////////////////////////

// 启用缓存
`ifndef DCACHE_DISABLE
`define DCACHE_ENABLE
`endif

`ifndef DCACHE_ENABLE
    `define NUM_DCACHES 0
    `define DCACHE_NUM_BANKS 1
`endif

// 缓存单元数量
`ifndef NUM_DCACHES
`define NUM_DCACHES `UP(`SOCKET_SIZE / 4)
`endif

// 缓存容量
`ifndef DCACHE_SIZE
`define DCACHE_SIZE 16384
`endif

// 存储体数量
`ifndef DCACHE_NUM_BANKS
`define DCACHE_NUM_BANKS `MIN(DCACHE_NUM_REQS, 16)
`endif

// 核心响应队列容量
`ifndef DCACHE_CRSQ_SIZE
`define DCACHE_CRSQ_SIZE 2
`endif

// 缺失处理寄存器容量
`ifndef DCACHE_MSHR_SIZE
`define DCACHE_MSHR_SIZE 16
`endif

// 存储器请求队列容量
`ifndef DCACHE_MREQ_SIZE
`define DCACHE_MREQ_SIZE 4
`endif

// 存储器响应队列容量
`ifndef DCACHE_MRSQ_SIZE
`define DCACHE_MRSQ_SIZE 4
`endif

// 组相联路数
`ifndef DCACHE_NUM_WAYS
`define DCACHE_NUM_WAYS 4
`endif

// 启用缓存写回
`ifndef DCACHE_WRITEBACK
`define DCACHE_WRITEBACK 0
`endif

// 启用缓存脏字节标记
`ifndef DCACHE_DIRTYBYTES
`define DCACHE_DIRTYBYTES `DCACHE_WRITEBACK
`endif

// 替换策略
`ifndef DCACHE_REPL_POLICY
`define DCACHE_REPL_POLICY 1
`endif

// 存储器端口数量
`ifndef L1_MEM_PORTS
`ifdef L1_DISABLE
`define L1_MEM_PORTS `MIN(DCACHE_NUM_REQS, `PLATFORM_MEMORY_NUM_BANKS)
`else
`define L1_MEM_PORTS `MIN(`DCACHE_NUM_BANKS, `PLATFORM_MEMORY_NUM_BANKS)
`endif
`endif

// 本地存储器可配置参数 ////////////////////////////////////////////////////

`ifndef LMEM_DISABLE
`define LMEM_ENABLE
`endif

`ifndef LMEM_ENABLE
    `define LMEM_NUM_BANKS 1
`endif

// 存储体数量
`ifndef LMEM_NUM_BANKS
`define LMEM_NUM_BANKS `NUM_LSU_LANES
`endif

// L2 缓存可配置参数 /////////////////////////////////////////////////

// 缓存容量
`ifndef L2_CACHE_SIZE
`define L2_CACHE_SIZE 1048576
`endif

// 存储体数量
`ifndef L2_NUM_BANKS
`define L2_NUM_BANKS `MIN(L2_NUM_REQS, 16)
`endif

// 核心响应队列容量
`ifndef L2_CRSQ_SIZE
`define L2_CRSQ_SIZE 2
`endif

// 缺失处理寄存器容量
`ifndef L2_MSHR_SIZE
`define L2_MSHR_SIZE 16
`endif

// 存储器请求队列容量
`ifndef L2_MREQ_SIZE
`define L2_MREQ_SIZE 4
`endif

// 存储器响应队列容量
`ifndef L2_MRSQ_SIZE
`define L2_MRSQ_SIZE 4
`endif

// 组相联路数
`ifndef L2_NUM_WAYS
`define L2_NUM_WAYS 8
`endif

// 启用缓存写回
`ifndef L2_WRITEBACK
`define L2_WRITEBACK 0
`endif

// 启用缓存脏字节标记
`ifndef L2_DIRTYBYTES
`define L2_DIRTYBYTES `L2_WRITEBACK
`endif

// 替换策略
`ifndef L2_REPL_POLICY
`define L2_REPL_POLICY 1
`endif

// 存储器端口数量
`ifndef L2_MEM_PORTS
`ifdef L2_ENABLE
`define L2_MEM_PORTS `MIN(`L2_NUM_BANKS, `PLATFORM_MEMORY_NUM_BANKS)
`else
`define L2_MEM_PORTS `MIN(L2_NUM_REQS, `PLATFORM_MEMORY_NUM_BANKS)
`endif
`endif

// L3 缓存可配置参数 /////////////////////////////////////////////////

// 缓存容量
`ifndef L3_CACHE_SIZE
`define L3_CACHE_SIZE 2097152
`endif

// 存储体数量
`ifndef L3_NUM_BANKS
`define L3_NUM_BANKS `MIN(L3_NUM_REQS, 16)
`endif

// 核心响应队列容量
`ifndef L3_CRSQ_SIZE
`define L3_CRSQ_SIZE 2
`endif

// 缺失处理寄存器容量
`ifndef L3_MSHR_SIZE
`define L3_MSHR_SIZE 16
`endif

// 存储器请求队列容量
`ifndef L3_MREQ_SIZE
`define L3_MREQ_SIZE 4
`endif

// 存储器响应队列容量
`ifndef L3_MRSQ_SIZE
`define L3_MRSQ_SIZE 4
`endif

// 组相联路数
`ifndef L3_NUM_WAYS
`define L3_NUM_WAYS 8
`endif

// 启用缓存写回
`ifndef L3_WRITEBACK
`define L3_WRITEBACK 0
`endif

// 启用缓存脏字节标记
`ifndef L3_DIRTYBYTES
`define L3_DIRTYBYTES `L3_WRITEBACK
`endif

// 替换策略
`ifndef L3_REPL_POLICY
`define L3_REPL_POLICY 1
`endif

// 存储器端口数量
`ifndef L3_MEM_PORTS
`ifdef L3_ENABLE
`define L3_MEM_PORTS `MIN(`L3_NUM_BANKS, `PLATFORM_MEMORY_NUM_BANKS)
`else
`define L3_MEM_PORTS `MIN(L3_NUM_REQS, `PLATFORM_MEMORY_NUM_BANKS)
`endif
`endif

// TCU 可配置参数 /////////////////////////////////////////////////////

`ifndef TCU_DRL
`ifndef TCU_BHF
`ifndef TCU_DSP
`ifndef TCU_DPI

`ifndef SYNTHESIS
`ifndef DPI_DISABLE
`define TCU_DPI
`else
`define TCU_BHF
`endif
`else
`define TCU_DSP
`endif

`endif
`endif
`endif
`endif

// ISA 扩展 /////////////////////////////////////////////////////////////

`ifdef ICACHE_ENABLE
    `define ICACHE_ENABLED 1
`else
    `define ICACHE_ENABLED 0
`endif

`ifdef DCACHE_ENABLE
    `define DCACHE_ENABLED 1
`else
    `define DCACHE_ENABLED 0
`endif

`ifdef LMEM_ENABLE
    `define LMEM_ENABLED 1
`else
    `define LMEM_ENABLED 0
`endif

`ifdef GBAR_ENABLE
    `define GBAR_ENABLED 1
`else
    `define GBAR_ENABLED 0
`endif

`ifdef L2_ENABLE
    `define L2_ENABLED 1
`else
    `define L2_ENABLED 0
`endif

`ifdef L3_ENABLE
    `define L3_ENABLED 1
`else
    `define L3_ENABLED 0
`endif

`ifdef EXT_A_ENABLE
    `define EXT_A_ENABLED   1
`else
    `define EXT_A_ENABLED   0
`endif

`ifdef EXT_C_ENABLE
    `define EXT_C_ENABLED   1
`else
    `define EXT_C_ENABLED   0
`endif

`ifdef EXT_D_ENABLE
    `define EXT_D_ENABLED   1
`else
    `define EXT_D_ENABLED   0
`endif

`ifdef EXT_F_ENABLE
    `define EXT_F_ENABLED   1
`else
    `define EXT_F_ENABLED   0
`endif

`ifdef EXT_M_ENABLE
    `define EXT_M_ENABLED   1
`else
    `define EXT_M_ENABLED   0
`endif

`ifdef EXT_V_ENABLE
    `define EXT_V_ENABLED   1
`else
    `define EXT_V_ENABLED   0
`endif

`ifdef EXT_ZICOND_ENABLE
    `define EXT_ZICOND_ENABLED 1
`else
    `define EXT_ZICOND_ENABLED 0
`endif

`ifdef EXT_TCU_ENABLE
    `define EXT_TCU_ENABLED 1
`else
    `define EXT_TCU_ENABLED 0
`endif

`define ISA_STD_A           0
`define ISA_STD_C           2
`define ISA_STD_D           3
`define ISA_STD_E           4
`define ISA_STD_F           5
`define ISA_STD_H           7
`define ISA_STD_I           8
`define ISA_STD_N           13
`define ISA_STD_Q           16
`define ISA_STD_S           18
`define ISA_STD_V           21

`define ISA_EXT_ICACHE      0
`define ISA_EXT_DCACHE      1
`define ISA_EXT_L2CACHE     2
`define ISA_EXT_L3CACHE     3
`define ISA_EXT_LMEM        4
`define ISA_EXT_ZICOND      5
`define ISA_EXT_TCU         6

`define MISA_EXT  (`ICACHE_ENABLED  << `ISA_EXT_ICACHE) \
                | (`DCACHE_ENABLED  << `ISA_EXT_DCACHE) \
                | (`L2_ENABLED      << `ISA_EXT_L2CACHE) \
                | (`L3_ENABLED      << `ISA_EXT_L3CACHE) \
                | (`LMEM_ENABLED    << `ISA_EXT_LMEM) \
                | (`EXT_ZICOND_ENABLED << `ISA_EXT_ZICOND) \
                | (`EXT_TCU_ENABLED << `ISA_EXT_TCU) \

`define MISA_STD  (`EXT_A_ENABLED <<  0) /* A - 原子指令扩展 */ \
                | (0 <<  1) /* B - 暂留给位操作扩展（原注释的历史定义） */ \
                | (`EXT_C_ENABLED <<  2) /* C - 压缩指令扩展 */ \
                | (`EXT_D_ENABLED <<  3) /* D - 双精度浮点扩展 */ \
                | (0 <<  4) /* E - RV32E 基础指令集 */ \
                | (`EXT_F_ENABLED << 5) /* F - 单精度浮点扩展 */ \
                | (0 <<  6) /* G - 存在额外的标准扩展 */ \
                | (0 <<  7) /* H - 实现虚拟机监控模式 */ \
                | (1 <<  8) /* I - RV32I/64I/128I 基础指令集 */ \
                | (0 <<  9) /* J - 保留 */ \
                | (0 << 10) /* K - 保留 */ \
                | (0 << 11) /* L - 暂留给位操作扩展（原注释的历史定义） */ \
                | (`EXT_M_ENABLED << 12) /* M - 整数乘除法扩展 */ \
                | (0 << 13) /* N - 支持用户级中断 */ \
                | (0 << 14) /* O - 保留 */ \
                | (0 << 15) /* P - 暂留给打包 SIMD 扩展 */ \
                | (0 << 16) /* Q - 四倍精度浮点扩展 */ \
                | (0 << 17) /* R - 保留 */ \
                | (0 << 18) /* S - 实现监管者模式 */ \
                | (0 << 19) /* T - 暂留给事务存储器扩展 */ \
                | (1 << 20) /* U - 实现用户模式 */ \
                | (`EXT_V_ENABLED << 21) /* V - 暂留给向量扩展（原注释的历史定义） */ \
                | (0 << 22) /* W - 保留 */ \
                | (1 << 23) /* X - 存在非标准扩展 */ \
                | (0 << 24) /* Y - 保留 */ \
                | (0 << 25) /* Z - 保留 */

// 设备标识 //////////////////////////////////////////////////////

`define VENDOR_ID           0
`define ARCHITECTURE_ID     0
`define IMPLEMENTATION_ID   0

`endif // VX_CONFIG_VH

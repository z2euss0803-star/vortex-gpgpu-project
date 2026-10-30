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

`ifndef VX_TYPES_VH
`define VX_TYPES_VH

// 设备配置寄存器 /////////////////////////////////////////////

`define VX_CSR_ADDR_BITS                12
`define VX_DCR_ADDR_BITS                12

`define VX_DCR_BASE_STATE_BEGIN         12'h001
`define VX_DCR_BASE_STARTUP_ADDR0       12'h001
`define VX_DCR_BASE_STARTUP_ADDR1       12'h002
`define VX_DCR_BASE_STARTUP_ARG0        12'h003
`define VX_DCR_BASE_STARTUP_ARG1        12'h004
`define VX_DCR_BASE_MPM_CLASS           12'h005
`define VX_DCR_BASE_STATE_END           12'h006

`define VX_DCR_BASE_STATE(addr)         ((addr) - `VX_DCR_BASE_STATE_BEGIN)
`define VX_DCR_BASE_STATE_COUNT         (`VX_DCR_BASE_STATE_END-`VX_DCR_BASE_STATE_BEGIN)

// 机器模式性能监测计数器类别 ////////////////////////////

`define VX_DCR_MPM_CLASS_NONE           0
`define VX_DCR_MPM_CLASS_CORE           1
`define VX_DCR_MPM_CLASS_MEM            2

// 用户浮点 CSR ///////////////////////////////////////////////////

`define VX_CSR_FFLAGS                   12'h001
`define VX_CSR_FRM                      12'h002
`define VX_CSR_FCSR                     12'h003

`define VX_CSR_SATP                     12'h180

`define VX_CSR_PMPCFG0                  12'h3A0
`define VX_CSR_PMPADDR0                 12'h3B0

`define VX_CSR_MSTATUS                  12'h300
`define VX_CSR_MISA                     12'h301
`define VX_CSR_MEDELEG                  12'h302
`define VX_CSR_MIDELEG                  12'h303
`define VX_CSR_MIE                      12'h304
`define VX_CSR_MTVEC                    12'h305

`define VX_CSR_MSCRATCH                 12'h340
`define VX_CSR_MEPC                     12'h341
`define VX_CSR_MCAUSE                   12'h342

`define VX_CSR_MNSTATUS                 12'h744

`define VX_CSR_MPM_BASE                 12'hB00
`define VX_CSR_MPM_BASE_H               12'hB80
`define VX_CSR_MPM_USER                 12'hB03
`define VX_CSR_MPM_USER_H               12'hB83

// 机器模式核心性能计数器（标准） ////////////////////

`define VX_CSR_MCYCLE                   12'hB00
`define VX_CSR_MCYCLE_H                 12'hB80
`define VX_CSR_MPM_RESERVED             12'hB01
`define VX_CSR_MPM_RESERVED_H           12'hB81
`define VX_CSR_MINSTRET                 12'hB02
`define VX_CSR_MINSTRET_H               12'hB82

// 机器模式核心性能计数器（类别 1） /////////////////////

// 性能统计：流水线
`define VX_CSR_MPM_SCHED_ID             12'hB03
`define VX_CSR_MPM_SCHED_ID_H           12'hB83
`define VX_CSR_MPM_SCHED_ST             12'hB04
`define VX_CSR_MPM_SCHED_ST_H           12'hB84
`define VX_CSR_MPM_IBUF_ST              12'hB05
`define VX_CSR_MPM_IBUF_ST_H            12'hB85
`define VX_CSR_MPM_SCRB_ST              12'hB06
`define VX_CSR_MPM_SCRB_ST_H            12'hB86
`define VX_CSR_MPM_OPDS_ST              12'hB07
`define VX_CSR_MPM_OPDS_ST_H            12'hB87
`define VX_CSR_MPM_SCRB_ALU             12'hB08
`define VX_CSR_MPM_SCRB_ALU_H           12'hB88
`define VX_CSR_MPM_SCRB_FPU             12'hB09
`define VX_CSR_MPM_SCRB_FPU_H           12'hB89
`define VX_CSR_MPM_SCRB_LSU             12'hB0A
`define VX_CSR_MPM_SCRB_LSU_H           12'hB8A
`define VX_CSR_MPM_SCRB_SFU             12'hB0B
`define VX_CSR_MPM_SCRB_SFU_H           12'hB8B
`define VX_CSR_MPM_SCRB_CSRS            12'hB0C
`define VX_CSR_MPM_SCRB_CSRS_H          12'hB8C
`define VX_CSR_MPM_SCRB_WCTL            12'hB0D
`define VX_CSR_MPM_SCRB_WCTL_H          12'hB8D
`define VX_CSR_MPM_SCRB_VPU             12'hB13
`define VX_CSR_MPM_SCRB_VPU_H           12'hB93
`define VX_CSR_MPM_SCRB_TCU             12'hB14
`define VX_CSR_MPM_SCRB_TCU_H           12'hB94
// 性能统计：存储系统
`define VX_CSR_MPM_IFETCHES             12'hB0E
`define VX_CSR_MPM_IFETCHES_H           12'hB8E
`define VX_CSR_MPM_LOADS                12'hB0F
`define VX_CSR_MPM_LOADS_H              12'hB8F
`define VX_CSR_MPM_STORES               12'hB10
`define VX_CSR_MPM_STORES_H             12'hB90
`define VX_CSR_MPM_IFETCH_LT            12'hB11
`define VX_CSR_MPM_IFETCH_LT_H          12'hB91
`define VX_CSR_MPM_LOAD_LT              12'hB12
`define VX_CSR_MPM_LOAD_LT_H            12'hB92

// 机器模式存储器性能计数器（类别 2） ///////////////////

// 性能统计：指令缓存
`define VX_CSR_MPM_ICACHE_READS         12'hB03     // 读取总次数
`define VX_CSR_MPM_ICACHE_READS_H       12'hB83
`define VX_CSR_MPM_ICACHE_MISS_R        12'hB04     // 读缺失次数
`define VX_CSR_MPM_ICACHE_MISS_R_H      12'hB84
`define VX_CSR_MPM_ICACHE_MSHR_ST       12'hB05     // MSHR 阻塞次数
`define VX_CSR_MPM_ICACHE_MSHR_ST_H     12'hB85
// 性能统计：数据缓存
`define VX_CSR_MPM_DCACHE_READS         12'hB06     // 读取总次数
`define VX_CSR_MPM_DCACHE_READS_H       12'hB86
`define VX_CSR_MPM_DCACHE_WRITES        12'hB07     // 写入总次数
`define VX_CSR_MPM_DCACHE_WRITES_H      12'hB87
`define VX_CSR_MPM_DCACHE_MISS_R        12'hB08     // 读缺失次数
`define VX_CSR_MPM_DCACHE_MISS_R_H      12'hB88
`define VX_CSR_MPM_DCACHE_MISS_W        12'hB09     // 写缺失次数
`define VX_CSR_MPM_DCACHE_MISS_W_H      12'hB89
`define VX_CSR_MPM_DCACHE_BANK_ST       12'hB0A     // 存储体冲突次数
`define VX_CSR_MPM_DCACHE_BANK_ST_H     12'hB8A
`define VX_CSR_MPM_DCACHE_MSHR_ST       12'hB0B     // MSHR 阻塞次数
`define VX_CSR_MPM_DCACHE_MSHR_ST_H     12'hB8B
// 性能统计：L2 缓存
`define VX_CSR_MPM_L2CACHE_READS        12'hB0C     // 读取总次数
`define VX_CSR_MPM_L2CACHE_READS_H      12'hB8C
`define VX_CSR_MPM_L2CACHE_WRITES       12'hB0D     // 写入总次数
`define VX_CSR_MPM_L2CACHE_WRITES_H     12'hB8D
`define VX_CSR_MPM_L2CACHE_MISS_R       12'hB0E     // 读缺失次数
`define VX_CSR_MPM_L2CACHE_MISS_R_H     12'hB8E
`define VX_CSR_MPM_L2CACHE_MISS_W       12'hB0F     // 写缺失次数
`define VX_CSR_MPM_L2CACHE_MISS_W_H     12'hB8F
`define VX_CSR_MPM_L2CACHE_BANK_ST      12'hB10     // 存储体冲突次数
`define VX_CSR_MPM_L2CACHE_BANK_ST_H    12'hB90
`define VX_CSR_MPM_L2CACHE_MSHR_ST      12'hB11     // MSHR 阻塞次数
`define VX_CSR_MPM_L2CACHE_MSHR_ST_H    12'hB91
// 性能统计：L3 缓存
`define VX_CSR_MPM_L3CACHE_READS        12'hB12     // 读取总次数
`define VX_CSR_MPM_L3CACHE_READS_H      12'hB92
`define VX_CSR_MPM_L3CACHE_WRITES       12'hB13     // 写入总次数
`define VX_CSR_MPM_L3CACHE_WRITES_H     12'hB93
`define VX_CSR_MPM_L3CACHE_MISS_R       12'hB14     // 读缺失次数
`define VX_CSR_MPM_L3CACHE_MISS_R_H     12'hB94
`define VX_CSR_MPM_L3CACHE_MISS_W       12'hB15     // 写缺失次数
`define VX_CSR_MPM_L3CACHE_MISS_W_H     12'hB95
`define VX_CSR_MPM_L3CACHE_BANK_ST      12'hB16     // 存储体冲突次数
`define VX_CSR_MPM_L3CACHE_BANK_ST_H    12'hB96
`define VX_CSR_MPM_L3CACHE_MSHR_ST      12'hB17     // MSHR 阻塞次数
`define VX_CSR_MPM_L3CACHE_MSHR_ST_H    12'hB97
// 性能统计：存储系统
`define VX_CSR_MPM_MEM_READS            12'hB18     // 读取总次数
`define VX_CSR_MPM_MEM_READS_H          12'hB98
`define VX_CSR_MPM_MEM_WRITES           12'hB19     // 写入总次数
`define VX_CSR_MPM_MEM_WRITES_H         12'hB99
`define VX_CSR_MPM_MEM_LT               12'hB1A     // 存储器访问延迟
`define VX_CSR_MPM_MEM_LT_H             12'hB9A
`define VX_CSR_MPM_MEM_BANK_ST          12'hB1E     // 存储体冲突次数
`define VX_CSR_MPM_MEM_BANK_ST_H        12'hB9E
// 性能统计：本地存储器
`define VX_CSR_MPM_LMEM_READS           12'hB1B     // 存储器读取次数
`define VX_CSR_MPM_LMEM_READS_H         12'hB9B
`define VX_CSR_MPM_LMEM_WRITES          12'hB1C     // 存储器写入次数
`define VX_CSR_MPM_LMEM_WRITES_H        12'hB9C
`define VX_CSR_MPM_LMEM_BANK_ST         12'hB1D     // 存储体冲突次数
`define VX_CSR_MPM_LMEM_BANK_ST_H       12'hB9D
// 性能统计：访存合并器
`define VX_CSR_MPM_COALESCER_MISS       12'hB1F     // 访存合并器未命中次数
`define VX_CSR_MPM_COALESCER_MISS_H     12'hB9F

// <添加自定义计数器：使用地址 hB03..B1F、hB83..hB9F>

// 机器信息寄存器 //////////////////////////////////////////////

`define VX_CSR_MVENDORID                12'hF11
`define VX_CSR_MARCHID                  12'hF12
`define VX_CSR_MIMPID                   12'hF13
`define VX_CSR_MHARTID                  12'hF14

// 向量 CSR

`define VX_CSR_VSTART                   12'h008
`define VX_CSR_VXSAT                    12'h009
`define VX_CSR_VXRM                     12'h00A
`define VX_CSR_VCSR                     12'h00F
`define VX_CSR_VL                       12'hC20
`define VX_CSR_VTYPE                    12'hC21
`define VX_CSR_VLENB                    12'hC22

// GPGU CSR

`define VX_CSR_THREAD_ID                12'hCC0
`define VX_CSR_WARP_ID                  12'hCC1
`define VX_CSR_CORE_ID                  12'hCC2
`define VX_CSR_ACTIVE_WARPS             12'hCC3
`define VX_CSR_ACTIVE_THREADS           12'hCC4     // 注意！LLVM 也使用此值

`define VX_CSR_NUM_THREADS              12'hFC0
`define VX_CSR_NUM_WARPS                12'hFC1
`define VX_CSR_NUM_CORES                12'hFC2
`define VX_CSR_LOCAL_MEM_BASE           12'hFC3

`endif // VX_TYPES_VH

// Host transactions run through the generated PS VIP's HPM ports.
// Only PS-local DDR preparation/inspection uses VIP backdoor APIs.
import axi_vip_pkg::*;
`define PSVIP dut.ps.ps_bd_i.ps.inst
`define BD dut.ps.ps_bd_i
localparam [39:0] GPU=40'h00a0000000, CDMA=40'h00b0010000;
localparam [39:0] GPIO=40'h00b0020000, PLBASE=40'h0500000000;
localparam [39:0] TX=40'h0011000000, RX=40'h0011010000;
bit access_active=0;
bit hpm1_narrow_read_active=0;
int hpm1_narrow_reads=0;
int hpm1_raw_reads=0;
reg [127:0] hpm1_last_raw_read;
time access_started;
reg [39:0] access_address;
int phase=0, dma_jobs=0, golden_words=0, gpu_rounds=0;
int soc_round=0,soc_job=0;
reg [3:0] four_core_seen=0;
always @(posedge dut.pl_clk0) if(phase==4)
  four_core_seen <= four_core_seen | dut.pl.gpu.vortex.g_clusters[0].cluster.g_sockets[0].socket.per_core_busy;
int hpm0_reads=0, hpm0_writes=0, hpm1_reads=0, hpm1_writes=0;
int hp_reads=0, hp_writes=0, ddr_reads=0, ddr_writes=0;
int gpu_reads=0, gpu_writes=0;
int gpu_flush_issued=0, ps_flush_accepted=0;
`include "ps_allocation_check.svh"
// VX_gpu_pkg.MEM_REQ_FLAG_FLUSH is bit 0 in the audited actual build.
`define GPU_MEM dut.pl.gpu.vortex.mem_bus_if[0]
reg [31:0] kernel_words[0:21];
wire resetn_observed=dut.axi_resetn[0];
wire ready_observed=dut.memory_ready;
// Observe the Verilog BD net; XSim cannot dereference a VHDL instance here.
wire ddr_resetn_observed=`BD.rst_ddr_peripheral_aresetn[0];
// Observe the two counters that report unknown-reset assertions at startup.
// These are observations only: no force, deposit, or assertion disabling.
`define IC_PENDING dut.pl.gpu.vortex.g_clusters[0].cluster.g_sockets[0].socket.icache.g_cache_wrap[0].cache_wrap.g_cache.cache.g_banks[0].bank.mshr_pending_size
`define DC_PENDING dut.pl.gpu.vortex.g_clusters[0].cluster.g_sockets[0].socket.dcache.g_cache_wrap[0].cache_wrap.g_cache.cache.g_banks[0].bank.mshr_pending_size
wire ic_pending_reset_observed=`IC_PENDING.reset;
wire dc_pending_reset_observed=`DC_PENDING.reset;
wire [31:0] ic_pending_size_observed=`IC_PENDING.size;
wire [31:0] dc_pending_size_observed=`DC_PENDING.size;
int startup_clock_count=0;
wire ic_checker_armed=`IC_PENDING.g_size_gt1.g_wide_step.verification_reset_seen;
wire dc_checker_armed=`DC_PENDING.g_size_gt1.g_wide_step.verification_reset_seen;
always @(posedge dut.pl_clk0) begin
  startup_clock_count++;
  if(startup_clock_count<=10)
    $display("STARTUP_RESET_OBSERVATION time=%0t ic_reset=%b dc_reset=%b ic_size=%h dc_size=%h",
      $time,ic_pending_reset_observed,dc_pending_reset_observed,
      ic_pending_size_observed,dc_pending_size_observed);
end
initial begin
  #200ns;
  if(ic_pending_reset_observed!==1 || dc_pending_reset_observed!==1 ||
     ic_pending_size_observed!==0 || dc_pending_size_observed!==0)
    $fatal(1,"LOOPBACK_FAIL STARTUP_RESET_PROPAGATION");
  $display("LOOPBACK_STARTUP_RESET_PASS both observed pending counters reset and zero before host access");
  if(ic_checker_armed!==1 || dc_checker_armed!==1)
    $fatal(1,"HW4_CHECKER_NOT_ARMED");
  $display("HW4_CHECKER_ARMED_PASS ic=1 dc=1");
end

initial begin
  $dumpfile("hw4_mosaic_soc.vcd");
  // Top-level phase/counters plus selected boundary handshakes; no GPU-wide dump.
  $dumpvars(1,tb_hw4_mosaic_soc);
  $dumpvars(0,dut.HPM_arvalid,dut.HPM_arready,dut.HPM_rvalid,dut.HPM_rready,
              dut.HPM_awvalid,dut.HPM_awready,dut.HPM_bvalid,dut.HPM_bready,
              dut.HP_arvalid,dut.HP_arready,dut.HP_rvalid,dut.HP_rready,
              dut.HP_awvalid,dut.HP_awready,dut.HP_bvalid,dut.HP_bready);
  $dumpvars(0,`BD.ps.maxigp1_araddr,`BD.ps.maxigp1_arsize,`BD.ps.maxigp1_arlen,
              `BD.ps.maxigp1_arvalid,`BD.ps.maxigp1_arready,
              `BD.ps.maxigp1_rdata,`BD.ps.maxigp1_rresp,
              `BD.ps.maxigp1_rvalid,`BD.ps.maxigp1_rready);
  // Capture PS HP read addresses and raw data from time zero. XSim only permits
  // one VCD and rejects adding dumpvars at a later simulation time.
  $dumpvars(0,`BD.ps.saxigp2_araddr,`BD.ps.saxigp2_arlen,`BD.ps.saxigp2_arsize,
              `BD.ps.saxigp2_arvalid,`BD.ps.saxigp2_arready,
              `BD.ps.saxigp2_rdata,`BD.ps.saxigp2_rvalid,`BD.ps.saxigp2_rready,
              `BD.ps.saxigp2_rresp,`BD.ps.saxigp2_rlast,`BD.ps.saxigp2_rid);
  $dumpvars(0,`GPU_MEM.req_valid,`GPU_MEM.req_ready,`GPU_MEM.req_data,
              dut.pl.gpu.vortex.mem_req_addr[0]);
  // Keep actual MIG AXI traffic/data for failure reconstruction; no memory deposit.
  $dumpvars(0,`BD.ddr.c0_ddr4_s_axi_awaddr,`BD.ddr.c0_ddr4_s_axi_awlen,
              `BD.ddr.c0_ddr4_s_axi_awsize,`BD.ddr.c0_ddr4_s_axi_awvalid,`BD.ddr.c0_ddr4_s_axi_awready,
              `BD.ddr.c0_ddr4_s_axi_wdata,`BD.ddr.c0_ddr4_s_axi_wstrb,`BD.ddr.c0_ddr4_s_axi_wlast,
              `BD.ddr.c0_ddr4_s_axi_wvalid,`BD.ddr.c0_ddr4_s_axi_wready,
              `BD.ddr.c0_ddr4_s_axi_araddr,`BD.ddr.c0_ddr4_s_axi_arlen,`BD.ddr.c0_ddr4_s_axi_arsize,
              `BD.ddr.c0_ddr4_s_axi_arvalid,`BD.ddr.c0_ddr4_s_axi_arready,
              `BD.ddr.c0_ddr4_s_axi_rdata,`BD.ddr.c0_ddr4_s_axi_rlast,
              `BD.ddr.c0_ddr4_s_axi_rvalid,`BD.ddr.c0_ddr4_s_axi_rready,
              `BD.ddr.c0_ddr4_s_axi_bvalid,`BD.ddr.c0_ddr4_s_axi_bready,
              `BD.ddr.c0_ddr4_s_axi_bresp,`BD.ddr.c0_ddr4_s_axi_rresp);
end
initial begin
  #5s;
  $fatal(1,"LOOPBACK_FAIL GLOBAL_TIMEOUT phase=%0d",phase);
end
always begin
  #1us;
  if(access_active && ($time-access_started)>100us)
    $fatal(1,"LOOPBACK_FAIL MMIO_TIMEOUT phase=%0d address=%h resetn=%b ready=%b",
           phase,access_address,resetn_observed,ready_observed);
end
always @(posedge dut.pl_clk0) begin
  if(phase==4 && `GPU_MEM.req_valid && `GPU_MEM.req_ready) begin
    if(`GPU_MEM.req_data.flags[0] === 1'b1) begin
      if(`GPU_MEM.req_data.rw !== 0 || dut.pl.gpu.vortex.mem_req_addr[0] !== (32'h10030000>>6))
        $fatal(1,"LOOPBACK_FAIL FLUSH_MAPPING_OR_DIRECTION");
      gpu_flush_issued++;
      $display("LOOPBACK_FLUSH_SOURCE issued=%0d time=%0t",gpu_flush_issued,$time);
    end else if(dut.pl.gpu.vortex.mem_req_addr[0] == (32'h10030000>>6))
      $fatal(1,"LOOPBACK_FAIL UNFLAGGED_GPU_RESERVED_ACCESS");
  end
  if(phase==4 && `BD.ps.saxigp2_arvalid && `BD.ps.saxigp2_arready) begin : check_ps_allocation
    reg [63:0] first_byte,last_byte;
    first_byte={15'd0,`BD.ps.saxigp2_araddr};
    last_byte=first_byte+((64'd1+`BD.ps.saxigp2_arlen)<<`BD.ps.saxigp2_arsize);
    validate_ps_allocation(first_byte,last_byte,`BD.ps.saxigp2_arlen,
                           `BD.ps.saxigp2_arsize,gpu_flush_issued,ps_flush_accepted);
    if(first_byte==64'h10030000) begin
      ps_flush_accepted++;
      $display("LOOPBACK_FLUSH_HP accepted=%0d time=%0t",ps_flush_accepted,$time);
    end
  end
  if(hpm1_narrow_read_active && `BD.ps.maxigp1_arvalid && `BD.ps.maxigp1_arready) begin
    if(`BD.ps.maxigp1_araddr !== access_address ||
       `BD.ps.maxigp1_arsize !== 3'd2 || `BD.ps.maxigp1_arlen !== 8'd0)
      $fatal(1,"LOOPBACK_FAIL HPM1_NARROW_READ_COMMAND addr=%h size=%h len=%h",
             `BD.ps.maxigp1_araddr,`BD.ps.maxigp1_arsize,`BD.ps.maxigp1_arlen);
    hpm1_narrow_reads++;
  end
  if(dut.HPM_rvalid && dut.HPM_rready) begin
    hpm0_reads++;
    if(dut.HPM_rresp!==0) $fatal(1,"LOOPBACK_FAIL HPM0_RRESP");
  end
  if(dut.HPM_bvalid && dut.HPM_bready) begin
    hpm0_writes++;
    if(dut.HPM_bresp!==0) $fatal(1,"LOOPBACK_FAIL HPM0_BRESP");
  end
  if(`BD.ps.maxigp1_rvalid && `BD.ps.maxigp1_rready) begin
    hpm1_reads++;
    if(hpm1_narrow_read_active) begin
      hpm1_last_raw_read=`BD.ps.maxigp1_rdata;
      hpm1_raw_reads++;
    end
    if(`BD.ps.maxigp1_rresp!==0) $fatal(1,"LOOPBACK_FAIL HPM1_RRESP");
  end
  if(`BD.ps.maxigp1_bvalid && `BD.ps.maxigp1_bready) begin
    hpm1_writes++;
    if(`BD.ps.maxigp1_bresp!==0) $fatal(1,"LOOPBACK_FAIL HPM1_BRESP");
  end
  if(`BD.ps.saxigp2_rvalid && `BD.ps.saxigp2_rready) begin
    hp_reads++;
    if(`BD.ps.saxigp2_rresp!==0) $fatal(1,"LOOPBACK_FAIL PS_HP_RRESP");
  end
  if(`BD.ps.saxigp2_bvalid && `BD.ps.saxigp2_bready) begin
    hp_writes++;
    if(`BD.ps.saxigp2_bresp!==0) $fatal(1,"LOOPBACK_FAIL PS_HP_BRESP");
  end
  if(dut.HP_rvalid && dut.HP_rready) begin
    gpu_reads++;
    if(dut.HP_rresp!==0) $fatal(1,"LOOPBACK_FAIL GPU_RRESP");
  end
  if(dut.HP_bvalid && dut.HP_bready) begin
    gpu_writes++;
    if(dut.HP_bresp!==0) $fatal(1,"LOOPBACK_FAIL GPU_BRESP");
  end
end
always @(posedge `BD.ddr.c0_ddr4_ui_clk) begin
  if(`BD.ddr.c0_ddr4_s_axi_rvalid && `BD.ddr.c0_ddr4_s_axi_rready) begin
    ddr_reads++;
    if(`BD.ddr.c0_ddr4_s_axi_rresp!==0) $fatal(1,"LOOPBACK_FAIL DDR_RRESP");
  end
  if(`BD.ddr.c0_ddr4_s_axi_bvalid && `BD.ddr.c0_ddr4_s_axi_bready) begin
    ddr_writes++;
    if(`BD.ddr.c0_ddr4_s_axi_bresp!==0) $fatal(1,"LOOPBACK_FAIL DDR_BRESP");
  end
end

task automatic eq32(input string label_, input [31:0] got, expected);
  if(got!==expected) $fatal(1,"LOOPBACK_FAIL %s expected=%h actual=%h phase=%0d",label_,expected,got,phase);
endtask
task automatic mmwrite(input [39:0] addr,input [31:0] value);
  reg [1:0] response;
  access_address=addr;access_started=$time;access_active=1;
  `PSVIP.write_data(addr,4,value,response);
  access_active=0;
  if(response!==0) $fatal(1,"LOOPBACK_FAIL WRITE_RESPONSE addr=%h resp=%b",addr,response);
  if(addr==CDMA+40'h28) dma_jobs++;
  if(addr==GPU+40'h8 && value==2) begin
    if(gpu_flush_issued!=ps_flush_accepted || gpu_flush_issued==0)
      $fatal(1,"HOST_FLUSH_SOURCE_HP_MISMATCH");
    gpu_rounds++;
  end
endtask
task automatic mmread(input [39:0] addr,output [31:0] value);
  reg [2047:0] data;
  reg [1:0] response;
  axi_transaction read_txn;
  axi_ready_gen read_ready;
  xil_axi_data_beat payload;
  int count_before;
  int response_count_before;
  access_address=addr;access_started=$time;access_active=1;
  if(addr[1:0] != 0) $fatal(1,"LOOPBACK_FAIL UNALIGNED_READ32 addr=%h",addr);
  if(addr[39:28] == GPU[39:28]) begin
    // HPM0 is 32 bits; the vendor helper's low word is the entire beat.
    `PSVIP.read_data(addr,4,data,response);
    value=data[31:0];
  end else begin
    if(!(addr[31:28] == 4'hb || addr[35:32] == 4'h5))
      $fatal(1,"LOOPBACK_FAIL UNKNOWN_HPM1_ADDRESS addr=%h",addr);
    // The convenience helper requests a bus-width-sized transfer and retains
    // the leading lane padding. Use one actual 32-bit transfer instead.
    hpm1_narrow_read_active=1;count_before=hpm1_narrow_reads;
    response_count_before=hpm1_raw_reads;
    read_ready=`PSVIP.M_AXI_HPM1_FPD.mst.rd_driver.create_ready("host_rready");
    read_ready.set_ready_policy(XIL_AXI_READY_GEN_OSC);
    `PSVIP.M_AXI_HPM1_FPD.mst.rd_driver.send_rready(read_ready);
    read_txn=`PSVIP.M_AXI_HPM1_FPD.mst.rd_driver.create_transaction("host_read32");
    if(!read_txn.randomize()) $fatal(1,"LOOPBACK_FAIL VIP_READ_RANDOMIZE");
    read_txn.set_read_cmd(addr,XIL_AXI_BURST_TYPE_INCR,0,0,XIL_AXI_SIZE_4BYTE);
    read_txn.set_cache(0);read_txn.set_lock(XIL_AXI_ALOCK_NOLOCK);read_txn.set_prot(0);
    read_txn.set_driver_return_item_policy(XIL_AXI_PAYLOAD_RETURN);
    `PSVIP.M_AXI_HPM1_FPD.mst.rd_driver.send(read_txn);
    `PSVIP.M_AXI_HPM1_FPD.mst.rd_driver.wait_rsp(read_txn);
    payload=read_txn.get_data_beat(0);response=read_txn.get_rresp(0);
    // axi_vip_pkg.sv get_data_beat() normalizes significant bytes to bit zero.
    // Only the independently captured raw RDATA needs the physical lane offset.
    value=payload[31:0];
    hpm1_narrow_read_active=0;
    if(hpm1_narrow_reads != count_before+1)
      $fatal(1,"LOOPBACK_FAIL HPM1_READ_NOT_OBSERVED addr=%h",addr);
    if(hpm1_raw_reads != response_count_before+1 ||
       value !== hpm1_last_raw_read[8*addr[3:0]+:32])
      $fatal(1,"LOOPBACK_FAIL HPM1_RAW_PAYLOAD_MISMATCH addr=%h raw=%h payload=%h",
             addr,hpm1_last_raw_read,value);
    if(phase==2) $display("HPM1_READ32 addr=%h lane=%0d raw=%h payload=%h value=%h",
                         addr,addr[3:0],hpm1_last_raw_read,payload[127:0],value);
  end
  access_active=0;
  if(response!==0) $fatal(1,"LOOPBACK_FAIL READ_RESPONSE addr=%h resp=%b",addr,response);
endtask
task automatic pswrite(input [39:0] addr,input [31:0] value);
  `PSVIP.write_mem(value,addr,4);
endtask
task automatic psread(input [39:0] addr,output [31:0] value);
  reg [32767:0] data;
  `PSVIP.read_mem(addr,4,data);value=data[31:0];
endtask

`include "host_access.svh"
import "DPI-C" context task host_entry(output int result);
export "DPI-C" task host_bus;
initial begin : shared_c_sequence
  int result;
  #1ns;
  `PSVIP.set_debug_level_info(0);
  `PSVIP.set_stop_on_error(1);
  `PSVIP.fpga_soft_reset(32'h1);
  `PSVIP.por_srstb_reset(1'b0);
  #1us;
  `PSVIP.por_srstb_reset(1'b1);
  #1us;
  if(`PSVIP.net_rstn!==1 || `PSVIP.ddrc.state!==2'd0 ||
     `PSVIP.S_AXI_HP0_FPD.rd_fifo_state!==1'b0)
    $fatal(1,"HOST_PS_POR_INITIALIZATION");
  `PSVIP.fpga_soft_reset(32'h0);
  wait(resetn_observed===1'b1);
  host_entry(result);
  if(result!=0) $fatal(1,"SHARED_HOST_C_FAILED result=%0d",result);
  $display("MOSAIC_SOC_FIRST_RUN_PASS");soc_round=1;
  phase=7;
  `PSVIP.fpga_soft_reset(32'h1);
  #2us;
  if(resetn_observed!==0) $fatal(1,"SHARED_HOST_RESET_NOT_ASSERTED");
  `PSVIP.fpga_soft_reset(32'h0);
  wait(resetn_observed===1'b1);
  host_entry(result);
  if(result!=0) $fatal(1,"SHARED_HOST_C_REPEAT_FAILED result=%0d",result);
  $display("MOSAIC_SOC_RESET_REPEAT_PASS");
  if(dma_jobs!=4416 || gpu_rounds!=20 || four_core_seen!=15 || gpu_flush_issued==0 || gpu_flush_issued!=ps_flush_accepted ||
     hpm0_reads==0 || hpm0_writes==0 || hpm1_reads==0 || hpm1_writes==0 ||
     hp_reads==0 || hp_writes==0 || ddr_reads==0 || ddr_writes==0 ||
     gpu_reads==0 || gpu_writes==0 || host_flushes==0 || host_invalidates==0)
    $fatal(1,"SHARED_HOST_BOUNDARY_COUNTS dma=%0d gpu=%0d flush_src=%0d hp=%0d",
      dma_jobs,gpu_rounds,gpu_flush_issued,ps_flush_accepted);
  phase=6;
  $display("MOSAIC_SOC_COUNTS DMA=%0d GPU=%0d HPM0_R=%0d HPM0_W=%0d HPM1_R=%0d HPM1_W=%0d HP_R=%0d HP_W=%0d DDR_R=%0d DDR_W=%0d GPU_R=%0d GPU_W=%0d PS_READ=%0d PS_WRITE=%0d FLUSH=%0d INVALIDATE=%0d",
    dma_jobs,gpu_rounds,hpm0_reads,hpm0_writes,hpm1_reads,hpm1_writes,hp_reads,hp_writes,
    ddr_reads,ddr_writes,gpu_reads,gpu_writes,host_reads,host_writes,host_flushes,host_invalidates);
  $display("MOSAIC_HW4_ACTUAL_SOC_TB_PASS rounds=2 jobs=20 checks=49840 guards=10880 cores=f; native production device driver + PS VIP/CDMA/GPU/DDR; independent audit pending; no A53 ISA/board");
  $dumpflush;$finish;
end
`undef PSVIP
`undef BD
`undef IC_PENDING
`undef DC_PENDING
`undef GPU_MEM

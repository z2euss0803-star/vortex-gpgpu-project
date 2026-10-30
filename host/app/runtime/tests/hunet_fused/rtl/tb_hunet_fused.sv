`timescale 1ns/1ps
`include "VX_define.vh"
module tb_hunet_fused;
 logic clk=0;always #5 clk=~clk;
 logic reset=1;logic dcr_wr_valid=0;logic[11:0]dcr_wr_addr=0;logic[31:0]dcr_wr_data=0;
 wire busy,tex_pending,tex_error;
 wire awvalid[1],awready[1],wvalid[1],wready[1],wlast[1],bvalid[1],bready[1];
 wire arvalid[1],arready[1],rvalid[1],rready[1],rlast[1];
 wire[31:0]awaddr[1],araddr[1];wire[5:0]awid[1],bid[1],arid[1],rid[1];
 wire[7:0]awlen[1],arlen[1];wire[511:0]wdata[1],rdata[1];wire[63:0]wstrb[1];wire[1:0]bresp[1],rresp[1];
 Vortex_axi #(.AXI_DATA_WIDTH(512),.AXI_ADDR_WIDTH(32),.AXI_TID_WIDTH(6),.AXI_NUM_BANKS(1)) dut(
  .clk(clk),.reset(reset),.dcr_wr_valid(dcr_wr_valid),.dcr_wr_addr(dcr_wr_addr),.dcr_wr_data(dcr_wr_data),
  .busy(busy),.tex_pending(tex_pending),.tex_error(tex_error),
  .m_axi_awvalid(awvalid),.m_axi_awready(awready),.m_axi_awaddr(awaddr),.m_axi_awid(awid),.m_axi_awlen(awlen),
  .m_axi_wvalid(wvalid),.m_axi_wready(wready),.m_axi_wdata(wdata),.m_axi_wstrb(wstrb),.m_axi_wlast(wlast),
  .m_axi_bvalid(bvalid),.m_axi_bready(bready),.m_axi_bid(bid),.m_axi_bresp(bresp),
  .m_axi_arvalid(arvalid),.m_axi_arready(arready),.m_axi_araddr(araddr),.m_axi_arid(arid),.m_axi_arlen(arlen),
  .m_axi_rvalid(rvalid),.m_axi_rready(rready),.m_axi_rdata(rdata),.m_axi_rid(rid),.m_axi_rresp(rresp),.m_axi_rlast(rlast));
 axi_memory memory(.clk(clk),.reset(reset),.awvalid(awvalid[0]),.awready(awready[0]),.awaddr(awaddr[0]),.awid(awid[0]),.awlen(awlen[0]),
  .wvalid(wvalid[0]),.wready(wready[0]),.wdata(wdata[0]),.wstrb(wstrb[0]),.wlast(wlast[0]),
  .bvalid(bvalid[0]),.bready(bready[0]),.bid(bid[0]),.bresp(bresp[0]),
  .arvalid(arvalid[0]),.arready(arready[0]),.araddr(araddr[0]),.arid(arid[0]),.arlen(arlen[0]),
  .rvalid(rvalid[0]),.rready(rready[0]),.rdata(rdata[0]),.rid(rid[0]),.rresp(rresp[0]),.rlast(rlast[0]));
 task dcr(input logic[11:0]a,input logic[31:0]v);
  @(negedge clk);dcr_wr_valid=1;dcr_wr_addr=a;dcr_wr_data=v;
  @(negedge clk);dcr_wr_valid=0;
 endtask
 reg[3:0]arrivals=0;integer released=0,first_read=0,cycles=0,expected_b=0,max_cycles=10000000;
 always @(posedge clk)if(!reset)begin
  cycles<=cycles+1;
  if(dut.vortex.g_clusters[0].cluster.gbar_bus_if.req_valid&&dut.vortex.g_clusters[0].cluster.gbar_bus_if.req_ready)begin
   arrivals[dut.vortex.g_clusters[0].cluster.gbar_bus_if.req_data.core_id]=1;
   $display("FUSED_GBAR_ARRIVE %0t core=%0d packed=%0d",$time,dut.vortex.g_clusters[0].cluster.gbar_bus_if.req_data.core_id,memory.packed_writes);
  end
  if(dut.vortex.g_clusters[0].cluster.gbar_bus_if.rsp_valid)begin
   if(arrivals!=15||memory.packed_writes<expected_b)$fatal(1,"PACK_NOT_DRAINED_AT_GBAR");
   released=1;$display("FUSED_GBAR_RELEASE %0t mask=%0d",$time,arrivals);
  end
  if(arvalid[0]&&arready[0]&&araddr[0]>=32'h86c4df40&&araddr[0]<32'h86d0df40)begin
   if(!released)$fatal(1,"GEMM_BEFORE_GLOBAL_HANDOFF");
   if(!first_read)$display("FUSED_FIRST_GEMM_READ %0t",$time);first_read=1;
  end
  if(cycles>max_cycles)$fatal(1,"RTL_CYCLE_TIMEOUT");
 end
 integer fd,rc,jobs=1;reg[63:0]a;reg[31:0]expected;string gold,gold2,overlay;
 initial begin
  // XSim 2020.1 corrupts the dynamic string argument to $dumpfile. A literal
  // filename is harvested into this case's directory after each natural exit.
  $dumpfile("fused.vcd");$dumpvars(1,tb_hunet_fused);
  // The matched production relay has uninitialized synchronous registers.
  // Bootstrap only these two derived reset nets; never force datapath/counters.
  // Release all forces while external reset remains asserted, then verify that
  // the real relay pipeline supplies 1 before the ordinary reset sequence.
  force dut.vortex.g_clusters[0].cluster.g_sockets[0].socket.icache_reset=1'b1;
  force dut.vortex.g_clusters[0].cluster.g_sockets[0].socket.dcache_reset=1'b1;
  $display("FUSED_TB_RESET_BOOTSTRAP_BEGIN %0t targets=socket.icache_reset,socket.dcache_reset",$time);
  repeat(8)@(posedge clk);@(negedge clk);
  release dut.vortex.g_clusters[0].cluster.g_sockets[0].socket.icache_reset;
  release dut.vortex.g_clusters[0].cluster.g_sockets[0].socket.dcache_reset;
  #1;
  if(reset!==1'b1||dut.vortex.g_clusters[0].cluster.g_sockets[0].socket.icache_reset!==1'b1||dut.vortex.g_clusters[0].cluster.g_sockets[0].socket.dcache_reset!==1'b1)$fatal(1,"RESET_RELAY_NOT_SETTLED");
  $display("FUSED_TB_RESET_BOOTSTRAP_RELEASED %0t external_reset=1 relay_resets=11 no_force_active",$time);
  if(!$value$plusargs("GOLD=%s",gold)||!$value$plusargs("PACK_BYTES=%d",expected_b))$fatal(1,"GOLD_ARGUMENT");
  void'($value$plusargs("MAX_CYCLES=%d",max_cycles));void'($value$plusargs("JOBS=%d",jobs));
  void'($value$plusargs("GOLD2=%s",gold2));void'($value$plusargs("OVERLAY=%s",overlay));
  for(integer job=0;job<jobs;job++)begin
  if(job!=0)begin @(negedge clk);reset=1;arrivals=0;released=0;first_read=0;cycles=0;
   memory.load_overlay(overlay);gold=gold2;end
  repeat(16)@(posedge clk);
  dcr(12'h001,32'h80000000);dcr(12'h003,32'h10010040);dcr(12'h005,0);
  repeat(16)@(posedge clk);@(negedge clk);reset=0;
  wait(busy);wait(!busy);repeat(30)@(posedge clk);
  if(arrivals!=15||!released||!first_read||tex_error)$fatal(1,"PHASE_COVERAGE");
  for(integer c=0;c<4;c++)if(memory.read32(32'h10002088+c*256)!==0)$fatal(1,"CRT_EXIT core=%0d",c);
  fd=$fopen(gold,"r");if(!fd)$fatal(1,"GOLD_OPEN");
  while(!$feof(fd))begin rc=$fscanf(fd,"%h %h\n",a,expected);
   if(rc==2&&memory.read32(a)!==expected)$fatal(1,"OUTPUT_MISMATCH addr=%h expected=%h actual=%h",a,expected,memory.read32(a));
  end
  $fclose(fd);$display("FUSED_MULTICORE_RTL_PASS job=%0d cycles=%0d barrier_mask=%0d",job,cycles,arrivals);
  end
  if(jobs==2)$display("FUSED_TWO_LAUNCH_REUSE_PASS");$finish;
 end
endmodule

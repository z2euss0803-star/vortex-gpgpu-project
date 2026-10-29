// Four-state simulation starts relay flops at X. Model initial reset on the
// two cache banks for five clocks only; release while the normal global reset
// remains asserted for another 55 clocks. No force remains during host traffic,
// job reset, kernel execution or repeated-job validation. All assertions stay on.
initial begin
  force dut.gpu.vortex.g_clusters[0].cluster.g_sockets[0].socket.icache.g_cache_wrap[0].cache_wrap.g_cache.cache.g_banks[0].bank.reset = 1'b1;
  force dut.gpu.vortex.g_clusters[0].cluster.g_sockets[0].socket.dcache.g_cache_wrap[0].cache_wrap.g_cache.cache.g_banks[0].bank.reset = 1'b1;
  #100;
  release dut.gpu.vortex.g_clusters[0].cluster.g_sockets[0].socket.icache.g_cache_wrap[0].cache_wrap.g_cache.cache.g_banks[0].bank.reset;
  release dut.gpu.vortex.g_clusters[0].cluster.g_sockets[0].socket.dcache.g_cache_wrap[0].cache_wrap.g_cache.cache.g_banks[0].bank.reset;
end
// Actual Vortex + actual Xilinx protocol converter, modeled HPM master / DDR.
always @(posedge clk) if(resetn) begin
  if(dut.dcr_wr_valid) $display("DCR addr=%h data=%h",dut.dcr_wr_addr,dut.dcr_wr_data);
  if(HP_arvalid && HP_arready) $display("AR addr=%h id=%h time=%t",HP_araddr,HP_arid,$time);
  if(HP_awvalid && HP_awready) $display("AW addr=%h time=%t",HP_awaddr,$time);
end
reg [31:0] memory [0:131071];
reg [31:0] kernel_words [0:21];
reg [3:0] four_core_seen=0;
always @(posedge clk) if(resetn) four_core_seen <= four_core_seen | dut.gpu.vortex.g_clusters[0].cluster.g_sockets[0].socket.per_core_busy;

integer cycle=0,reads=0,writes=0,round,i,j,index;
integer ps_reads=0,pl_reads=0,ps_writes=0,pl_writes=0,partial_writes=0;
integer vec_bank=0;
reg [63:0] covered=0;
reg aw_full=0,w_full=0,r_full=0;
reg [48:0] write_addr,read_addr;
reg [511:0] write_data;
reg [63:0] write_strb;
reg [5:0] write_id,read_id;
integer read_delay=0,write_delay=0;
reg [31:0] value,expected;
assign HP_awready=resetn && !aw_full && !HP_bvalid && cycle%3!=0;
assign HP_wready=resetn && !w_full && !HP_bvalid && cycle%5!=0;
assign HP_arready=resetn && !r_full && !HP_rvalid && cycle%4!=0;
function automatic integer mem_index(input[48:0] addr);
  if(addr>=49'h500000000 && addr<49'h500040000) mem_index=65536+(addr-49'h500000000)/4;
  else mem_index=(addr-49'h10000000)/4;
endfunction
task automatic legal_address(input[48:0] addr);
  if(!((addr>=49'h10000000 && addr<49'h10040000) ||
       (addr>=49'h500000000 && addr<49'h500040000)) || addr[5:0]!=0)
    $fatal(1,"DDR range/alignment violation: %h",addr);
endtask
always @(posedge clk) begin
  if(!resetn) begin
    cycle<=0;aw_full<=0;w_full<=0;r_full<=0;
    HP_bvalid<=0;HP_rvalid<=0;HP_bresp<=0;HP_rresp<=0;HP_rlast<=1;
    HP_bid<=0;HP_rid<=0;HP_rdata<=0;write_delay<=0;read_delay<=0;
  end else begin
    cycle<=cycle+1;
    if(HP_awvalid && HP_awready) begin
      legal_address(HP_awaddr);
      if(HP_awlen!=0 || HP_awsize!=6) $fatal(1,"unexpected AW size/burst");
      write_addr<=HP_awaddr;write_id<=HP_awid;aw_full<=1;
    end
    if(HP_wvalid && HP_wready) begin
      if(!HP_wlast)$fatal(1,"missing WLAST");
      write_data<=HP_wdata;write_strb<=HP_wstrb;w_full<=1;
    end
    if(aw_full && w_full && !HP_bvalid) begin
      // Delayed B stresses the completion drain after the last store.
      if(write_delay==24) begin
        for(j=0;j<64;j=j+1) if(write_strb[j]) begin
          index=mem_index(write_addr)+j/4;
          memory[index][8*(j%4)+:8]<=write_data[j*8+:8];
        end
        for(j=0;j<16;j=j+1) if(write_strb[4*j+:4]==15 && mem_index(write_addr)+j>=vec_bank+'h20240/4 && mem_index(write_addr)+j<vec_bank+'h20340/4)
          covered[mem_index(write_addr)+j-vec_bank-'h20240/4]<=1;
        if(write_addr[32]) pl_writes<=pl_writes+1; else ps_writes<=ps_writes+1;
        if(write_strb!=64'hffffffffffffffff) partial_writes<=partial_writes+1;
        HP_bvalid<=1;HP_bid<=write_id;write_delay<=0;aw_full<=0;w_full<=0;writes<=writes+1;
      end else write_delay<=write_delay+1;
    end
    if(HP_bvalid && HP_bready) HP_bvalid<=0;
    if(HP_arvalid && HP_arready) begin
      legal_address(HP_araddr);
      if(HP_arlen!=0 || HP_arsize!=6)$fatal(1,"unexpected AR size/burst");
      read_addr<=HP_araddr;read_id<=HP_arid;r_full<=1;read_delay<=0;
    end
    if(r_full && !HP_rvalid) begin
      if(read_delay==4) begin
        for(j=0;j<16;j=j+1) HP_rdata[j*32+:32]<=memory[mem_index(read_addr)+j];
        if(read_addr[32]) pl_reads<=pl_reads+1; else ps_reads<=ps_reads+1;
        HP_rid<=read_id;HP_rvalid<=1;r_full<=0;reads<=reads+1;
      end else read_delay<=read_delay+1;
    end
    if(HP_rvalid && HP_rready) HP_rvalid<=0;
  end
end
task automatic send_aw(input[39:0] addr);
  begin @(negedge clk);HPM_awaddr=addr;HPM_awid=16'h1234;HPM_awsize=2;HPM_awburst=1;HPM_awvalid=1;
    do @(posedge clk);while(!HPM_awready);
    @(negedge clk);HPM_awvalid=0;
  end
endtask
task automatic send_w(input[31:0] data);
  begin repeat(3)@(negedge clk);HPM_wdata=data;HPM_wstrb=15;HPM_wlast=1;HPM_wvalid=1;
    do @(posedge clk);while(!HPM_wready);
    @(negedge clk);HPM_wvalid=0;
  end
endtask
task automatic wr(input integer offset,input[31:0] data);
  begin fork send_aw(40'ha0000000+offset);send_w(data);join
    while(!HPM_bvalid)@(negedge clk);
    repeat(2)@(negedge clk);
    if(HPM_bresp!=0 || HPM_bid!=16'h1234)$fatal(1,"control write failed off=%h resp=%h",offset,HPM_bresp);
    HPM_bready=1;@(negedge clk);HPM_bready=0;
  end
endtask
task automatic rd(input integer offset);
  begin @(negedge clk);HPM_araddr=40'ha0000000+offset;HPM_arid=16'h6789;HPM_arsize=2;HPM_arburst=1;HPM_arvalid=1;
    do @(posedge clk);while(!HPM_arready);
    @(negedge clk);HPM_arvalid=0;
    while(!HPM_rvalid)@(negedge clk);
    repeat(2)@(negedge clk);
    if(HPM_rresp!=0 || HPM_rid!=16'h6789 || !HPM_rlast)$fatal(1,"control read failed");
    value=HPM_rdata;HPM_rready=1;@(negedge clk);HPM_rready=0;
  end
endtask
`include "gpu_extension_tests.svh"
initial begin
  $readmemh("vecadd16.hex",kernel_words);
  for(i=0;i<131072;i=i+1)memory[i]=0;
  for(i=0;i<22;i=i+1)memory[i]=kernel_words[i];
  repeat(60)@(negedge clk);resetn=1;repeat(50)@(negedge clk);
  $display("INITIAL_RESET_COMPLETE: all normal reset paths settled before HPM traffic");
  rd(0);if(value!=32'h56580204)$fatal(1,"ID mismatch");
  rd(4);if(value!=32'h00040404)$fatal(1,"configuration mismatch");
  for(round=0;round<3;round=round+1)begin
    vec_bank=(round==1 ? 65536 : 0);
    for(i=0;i<22;i=i+1)memory[vec_bank+i]=kernel_words[i];
    if(vec_bank!=0) memory[vec_bank+1]=32'h80000337;
    memory[vec_bank+'h10000/4]=(vec_bank ? 32'h80020000 : 32'h10020000);memory[vec_bank+'h10004/4]=(vec_bank ? 32'h10020100 : 32'h80020100);memory[vec_bank+'h10008/4]=(vec_bank ? 32'h80020240 : 32'h10020240);
    for(i=0;i<64;i=i+1)begin
      memory[vec_bank+'h20000/4+i]=32'h12340000+round*101+i*7;
      memory[65536-vec_bank+'h20100/4+i]=32'h80000100-round*13-i*3;
      memory[vec_bank+'h20240/4+i]=32'hdeadbeef;
    end
    for(i=0;i<16;i=i+1)begin
      memory[vec_bank+'h20200/4+i]=32'h55aa55aa;memory[vec_bank+'h20340/4+i]=32'haa55aa55;
    end
    covered=0;wr(16,vec_bank ? 32'h80000000 : 32'h10000000);wr(20,vec_bank ? 32'h80010000 : 32'h10010000);wr(24,1000000);wr(8,1);
    value=0;
    while(!value[1])begin
      repeat(20)@(negedge clk);rd(12);
      if(value[2])begin rd(28);$fatal(1,"GPU error bits=%h",value);end
    end
    if(covered!==64'hffff_ffff_ffff_ffff)$fatal(1,"not all four-core results stored: %h",covered);
    for(i=0;i<64;i=i+1)begin
      expected=memory[vec_bank+'h20000/4+i]+memory[65536-vec_bank+'h20100/4+i];
      if(memory[vec_bank+'h20240/4+i]!==expected)$fatal(1,"vecadd[%0d] expected %h actual %h",i,expected,memory[vec_bank+'h20240/4+i]);
    end
    for(i=0;i<16;i=i+1)
      if(memory[vec_bank+'h20200/4+i]!==32'h55aa55aa || memory[vec_bank+'h20340/4+i]!==32'haa55aa55)
        $fatal(1,"guard damaged i=%0d left=%h right=%h",i,memory[vec_bank+'h20200/4+i],memory[vec_bank+'h20340/4+i]);
    rd(32);if(value!=0)$fatal(1,"pending writes at DONE");
    rd(36);if(value!=0)$fatal(1,"pending reads at DONE");
    rd(44);if(value!=round+1)$fatal(1,"wrong job count");
    $display("GPU_RTL_ROUND_PASS round=%0d all64threads reads=%0d writes=%0d cycle=%0d",round,reads,writes,cycle);
    wr(8,2);
  end
  $display("GPGPU_VECADD_REGRESSION_PASS: 3 rounds / 192 sums");
  run_extended_tests;
  if(four_core_seen !== 4'hf) $fatal(1,"not all four cores became busy: %b",four_core_seen);
  if(ps_reads==0 || pl_reads==0 || ps_writes==0 || pl_writes==0 || partial_writes==0)
    $fatal(1,"missing dual DDR / partial write coverage");
  $display("DUAL_DDR_COVERAGE_PASS PS_R=%0d PL_R=%0d PS_W=%0d PL_W=%0d partial=%0d",ps_reads,pl_reads,ps_writes,pl_writes,partial_writes);
  $display("GPGPU_REAL_RTL_PASS: vecadd and native TCU/TEX through shared HP");$finish;
end
initial begin #30000000;$fatal(1,"GPU RTL global timeout");end

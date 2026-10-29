`timescale 1ns/1ps
// Controller boundary test; the core busy and HP response signals are modeled.
module tb_gpgpu_ctrl;
  reg clk=0; always #5 clk=~clk;
  reg resetn=0;
  reg memory_ready=1;
  reg [39:0] s_axi_awaddr=0,s_axi_araddr=0;
  reg [31:0] s_axi_wdata=0;
  reg [3:0] s_axi_wstrb=0;
  reg s_axi_awvalid=0,s_axi_wvalid=0,s_axi_bready=0,s_axi_arvalid=0,s_axi_rready=0;
  wire s_axi_awready,s_axi_wready,s_axi_bvalid,s_axi_arready,s_axi_rvalid;
  wire [31:0] s_axi_rdata;
  wire [1:0] s_axi_bresp,s_axi_rresp;
  reg gpu_busy=0;
  reg tex_pending=0,tex_error=0;
  reg mem_awvalid=0,mem_awready=0,mem_wvalid=0,mem_wready=0,mem_wlast=1;
  reg mem_bvalid=0,mem_bready=1,mem_arvalid=0,mem_arready=0,mem_rvalid=0,mem_rready=1,mem_rlast=1;
  reg [1:0] mem_bresp=0,mem_rresp=0;
  wire gpu_reset,dcr_wr_valid;
  wire [11:0] dcr_wr_addr;
  wire [31:0] dcr_wr_data;
  integer checks=0,dcr_count=0;
  reg [31:0] read_value;
  localparam [39:0] BASE=40'ha0000000;
  gpgpu_ctrl dut(.*);
  always @(posedge clk) if(resetn && dcr_wr_valid) begin
    if(!gpu_reset) $fatal(1,"DCR changed after reset released");
    case(dcr_wr_addr)
      1: if(dcr_wr_data!==dut.pc) $fatal(1,"PC DCR wrong");
      3: if(dcr_wr_data!==dut.arg_addr) $fatal(1,"argument DCR wrong");
      5: if(dcr_wr_data!==0) $fatal(1,"perf DCR wrong");
      'h100,'h101,'h102,'h103,'h104:;
      default: $fatal(1,"unexpected DCR");
    endcase
    dcr_count=dcr_count+1;
  end
  task automatic aw(input[39:0] addr,input integer delay_cycles);
    begin repeat(delay_cycles) @(negedge clk); @(negedge clk);
      s_axi_awaddr=addr; s_axi_awvalid=1;
      do @(posedge clk); while(!s_axi_awready);
      @(negedge clk); s_axi_awvalid=0; s_axi_awaddr=0;
    end
  endtask
  task automatic wd(input[31:0] value,input[3:0] strb,input integer delay_cycles);
    begin repeat(delay_cycles) @(negedge clk); @(negedge clk);
      s_axi_wdata=value;s_axi_wstrb=strb;s_axi_wvalid=1;
      do @(posedge clk); while(!s_axi_wready);
      @(negedge clk); s_axi_wvalid=0;
    end
  endtask
  task automatic wr(input[39:0] addr,input[31:0] value,input[3:0] strb,input[1:0] resp);
    reg[1:0] held;
    begin fork aw(addr,checks%4); wd(value,strb,(checks+1)%4); join
      while(!s_axi_bvalid) @(negedge clk);
      held=s_axi_bresp;
      repeat(4) begin @(negedge clk); if(!s_axi_bvalid || s_axi_bresp!==held) $fatal(1,"unstable B"); end
      if(held!==resp) $fatal(1,"write %h expected BRESP=%h got %h",addr,resp,held);
      s_axi_bready=1; @(negedge clk); s_axi_bready=0; checks=checks+1;
    end
  endtask
  task automatic rd(input[39:0] addr,input[1:0] resp);
    reg[31:0] held;
    begin @(negedge clk);s_axi_araddr=addr;s_axi_arvalid=1;
      do @(posedge clk);while(!s_axi_arready);
      @(negedge clk);s_axi_arvalid=0;
      while(!s_axi_rvalid) @(negedge clk);
      held=s_axi_rdata;
      repeat(3) begin @(negedge clk);if(!s_axi_rvalid || s_axi_rdata!==held || s_axi_rresp!==resp) $fatal(1,"unstable/error R");end
      read_value=held;s_axi_rready=1;@(negedge clk);s_axi_rready=0;checks=checks+1;
    end
  endtask
  task automatic check_rd(input[39:0] addr,input[31:0] value);
    begin rd(addr,0);if(read_value!==value)$fatal(1,"read %h expected %h got %h",addr,value,read_value);end
  endtask
  task automatic reset_bus;
    begin @(negedge clk);resetn=0;gpu_busy=0;
      mem_awvalid=0;mem_wvalid=0;mem_arvalid=0;mem_bvalid=0;mem_rvalid=0;
      repeat(10) @(negedge clk);resetn=1;repeat(4) @(negedge clk);
    end
  endtask
  task automatic start_job;
    begin if(gpu_reset) gpu_busy=1; wr(BASE+8,1,15,0);gpu_busy=0;wait(!gpu_reset);@(negedge clk);end
  endtask
  initial begin
    $dumpfile("gpgpu_ctrl.vcd");$dumpvars(0,tb_gpgpu_ctrl);
    reset_bus;
    memory_ready=0;
    wr(BASE+8,1,15,2);
    check_rd(BASE+'h34,0);
    if(!gpu_reset) $fatal(1,"start released GPU before DDR ready");
    memory_ready=1;
    check_rd(BASE+'h34,1);
    check_rd(BASE+48,32'h0000000f);
    check_rd(BASE,32'h56580204);check_rd(BASE+4,32'h00040404);
    wr(BASE+'h50,8,15,0);wr(BASE+8,1,15,2);
    wr(BASE+'h40,32'h10040000,15,0);wr(BASE+'h44,3,15,0);wr(BASE+'h48,5,15,0);
    wr(BASE+'h4c,20,15,0);wr(BASE+'h54,92,15,0);
    check_rd(BASE+'h54,92);
    wr(BASE+'h54,91,15,0);wr(BASE+8,1,15,2);
    wr(BASE+'h54,32'hffffffff,15,0);wr(BASE+8,1,15,2);
    wr(BASE+'h54,92,15,0);wr(BASE+'h50,0,15,0);
    rd(BASE+40'h1000,2);wr(BASE+40'h1008,1,15,2);
    wr(BASE+16,32'h10000000,15,0);wr(BASE+20,32'h10010000,15,0);
    wr(BASE+24,2000,15,0);
    wr(BASE+16,32'h40,1,0);check_rd(BASE+16,32'h10000040);
    wr(BASE+16,32'h10000000,15,0);
    wr(BASE,0,15,2);rd(BASE+1,2);rd(BASE+40'h100000000,2);
    wr(BASE+8,1,0,0);check_rd(BASE+12,0);
    wr(BASE+20,32'h30000000,15,0);wr(BASE+8,1,15,2);wr(BASE+20,32'h10010000,15,0);
    start_job;
    // Initially !busy must NOT report DONE; controller waits for true busy.
    repeat(15) @(negedge clk);rd(BASE+12,0);if(read_value[1])$fatal(1,"premature done before busy");
    wr(BASE+16,32'h10000400,15,2);wr(BASE+8,1,15,2);
    wr(BASE+'h44,2,15,2);
    gpu_busy=1;repeat(8)@(negedge clk);
    // Accept AW and W on different cycles, then hold the final B response.
    mem_awvalid=1;mem_awready=1;@(negedge clk);mem_awvalid=0;
    repeat(3)@(negedge clk);mem_wvalid=1;mem_wready=1;@(negedge clk);mem_wvalid=0;
    mem_arvalid=1;mem_arready=1;@(negedge clk);mem_arvalid=0;
    gpu_busy=0;repeat(15)@(negedge clk);
    rd(BASE+12,0);if(read_value[1])$fatal(1,"DONE with unreturned B/R");
    check_rd(BASE+32,1);check_rd(BASE+36,1);
    mem_bvalid=1;@(negedge clk);mem_bvalid=0;
    repeat(10)@(negedge clk);rd(BASE+12,0);if(read_value[1])$fatal(1,"DONE with pending R");
    tex_pending=1;mem_rvalid=1;@(negedge clk);mem_rvalid=0;
    repeat(20)@(negedge clk);check_rd(BASE+'h58,1);rd(BASE+12,0);
    if(read_value[1])$fatal(1,"DONE while TEX pending");tex_pending=0;
    repeat(20)@(negedge clk);rd(BASE+12,0);if(!read_value[1] || read_value[2])$fatal(1,"expected successful DONE");
    check_rd(BASE+44,1);wr(BASE+8,2,15,0);
    // A failed write response must remain visible after completion.
    start_job;gpu_busy=1;repeat(8)@(negedge clk);
    mem_awvalid=1;mem_wvalid=1;@(negedge clk);mem_awvalid=0;mem_wvalid=0;
    mem_bresp=2;mem_bvalid=1;@(negedge clk);mem_bvalid=0;mem_bresp=0;
    gpu_busy=0;repeat(25)@(negedge clk);rd(BASE+12,0);
    if(!read_value[1] || !read_value[2])$fatal(1,"AXI error missing from completed status");
    check_rd(BASE+28,1);wr(BASE+8,2,15,0);
    // RRESP errors are independently sticky.
    start_job;gpu_busy=1;repeat(8)@(negedge clk);
    mem_arvalid=1;@(negedge clk);mem_arvalid=0;
    mem_rresp=2;mem_rvalid=1;@(negedge clk);mem_rvalid=0;mem_rresp=0;
    gpu_busy=0;repeat(25)@(negedge clk);check_rd(BASE+28,2);wr(BASE+8,2,15,0);
    // Timeout is a locked fault; no unsafe reset/drop of in-flight AXI.
    wr(BASE+24,128,15,0);start_job;gpu_busy=1;
    repeat(150)@(negedge clk);rd(BASE+12,0);
    if(!read_value[2] || read_value[1] || gpu_reset)$fatal(1,"unsafe/missing timeout fault");
    wr(BASE+8,2,15,2);wr(BASE+8,1,15,2);
    reset_bus;check_rd(BASE+28,0);check_rd(BASE+44,0);
    if(dcr_count!=32)$fatal(1,"DCR count expected32 got%0d",dcr_count);
    // Legal ends include the final aligned word; neither window may wrap.
    reset_bus; wr(BASE+16,32'h2ffffffc,15,0);wr(BASE+20,32'hfffffffc,15,0);wr(BASE+8,1,15,0);
    reset_bus; wr(BASE+16,32'h80000000,15,0);wr(BASE+20,32'h10000000,15,0);wr(BASE+8,1,15,0);
    reset_bus; wr(BASE+16,32'h0ffffffc,15,0);wr(BASE+8,1,15,2);
    wr(BASE+16,32'h30000000,15,0);wr(BASE+8,1,15,2);
    wr(BASE+16,32'h7ffffffc,15,0);wr(BASE+8,1,15,2);
    wr(BASE+16,32'h80000002,15,0);wr(BASE+8,1,15,2);
    wr(BASE+16,32'h10000000,15,0);wr(BASE+20,32'hffffffff,15,0);wr(BASE+8,1,15,2);
    reset_bus;wr(BASE+'h40,32'hffffffc0,15,0);wr(BASE+'h44,16,15,0);wr(BASE+'h48,1,15,0);
    wr(BASE+'h4c,64,15,0);wr(BASE+'h54,65,15,0);wr(BASE+'h50,8,15,0);wr(BASE+8,1,15,2);
    wr(BASE+'h54,64,15,0);wr(BASE+8,1,15,0);
    reset_bus;wr(BASE+'h40,32'h2fffffc0,15,0);wr(BASE+'h44,16,15,0);wr(BASE+'h48,1,15,0);
    wr(BASE+'h4c,64,15,0);wr(BASE+'h54,65,15,0);wr(BASE+'h50,8,15,0);wr(BASE+8,1,15,2);
    wr(BASE+'h54,64,15,0);wr(BASE+8,1,15,0);
    // Loss of readiness with an outstanding read locks FAULT without resetting GPU.
    reset_bus;start_job;gpu_busy=1;repeat(8)@(negedge clk);
    mem_arvalid=1;mem_arready=1;@(negedge clk);mem_arvalid=0;
    memory_ready=0;repeat(4)@(negedge clk);check_rd(BASE+'h34,0);check_rd(BASE+28,32);
    check_rd(BASE+36,1);if(gpu_reset)$fatal(1,"ready loss reset an in-flight core");
    rd(BASE+12,0);if(read_value[1])$fatal(1,"ready loss manufactured DONE");
    mem_rvalid=1;@(negedge clk);mem_rvalid=0;check_rd(BASE+36,0);
    memory_ready=1;wr(BASE+8,1,15,2);wr(BASE+8,2,15,2);
    reset_bus;check_rd(BASE+'h38,32'h00010001);
    $display("GPGPU_CTRL_PASS: checks=%0d",checks);$finish;
  end
  initial begin #200000;$fatal(1,"test timeout");end
endmodule

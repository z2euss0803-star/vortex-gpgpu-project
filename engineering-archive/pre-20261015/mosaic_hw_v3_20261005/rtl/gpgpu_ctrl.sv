`timescale 1ns/1ps
// Single-job control ABI. The AXI-Lite address is the full PS physical address.
module gpgpu_ctrl (
  input wire clk, resetn,
  input wire [39:0] s_axi_awaddr, s_axi_araddr,
  input wire [31:0] s_axi_wdata,
  input wire [3:0] s_axi_wstrb,
  input wire s_axi_awvalid, s_axi_wvalid, s_axi_bready,
  input wire s_axi_arvalid, s_axi_rready,
  output wire s_axi_awready, s_axi_wready, s_axi_arready,
  output reg s_axi_bvalid, s_axi_rvalid,
  output reg [1:0] s_axi_bresp, s_axi_rresp,
  output reg [31:0] s_axi_rdata,
  input wire gpu_busy, memory_ready,
  input wire tex_pending,tex_error,
  input wire mem_awvalid, mem_awready, mem_wvalid, mem_wready, mem_wlast,
  input wire mem_bvalid, mem_bready,
  input wire [1:0] mem_bresp,
  input wire mem_arvalid, mem_arready, mem_rvalid, mem_rready, mem_rlast,
  input wire [1:0] mem_rresp,
  output reg gpu_reset,
  output wire dcr_wr_valid,
  output wire [11:0] dcr_wr_addr,
  output wire [31:0] dcr_wr_data
);
  localparam [39:0] BASE=40'h00a0000000;
  localparam [2:0] IDLE=0, INIT=1, WAIT_BUSY=2, RUN=3, DRAIN=4, DONE=5, FAULT=6;
  reg [2:0] state;
  reg [5:0] init_cycle;
  reg [3:0] quiet_cycles;
  reg [31:0] pc, arg_addr, watchdog, errors, elapsed, jobs;
  reg [31:0] tex_base,tex_width,tex_height,tex_pitch,tex_mode,tex_bytes;
  wire [63:0] tex_footprint=({32'd0,tex_height}-64'd1)*{32'd0,tex_pitch}+({32'd0,tex_width}<<2);
  wire [63:0] tex_end={32'd0,tex_base}+{32'd0,tex_bytes};
  wire tex_desc_ok=!tex_mode[3] || (tex_mode[31:4]==0 && tex_width>=1 && tex_width<=4096 &&
    tex_height>=1 && tex_height<=4096 && tex_base[5:0]==0 && tex_pitch[1:0]==0 &&
    {32'd0,tex_pitch}>=({32'd0,tex_width}<<2) && tex_footprint<={32'd0,tex_bytes} &&
    ((tex_base>=32'h10000000 && tex_base<32'h30000000 && tex_end<=64'h30000000) ||
     (tex_base>=32'h80000000 && tex_end<=64'h100000000)));
  reg [31:0] pending_aw, pending_w, pending_ar;
  reg aw_held,w_held;
  reg [39:0] awaddr;
  reg [31:0] wdata;
  reg [3:0] wstrb;
  wire active=(state==INIT || state==WAIT_BUSY || state==RUN || state==DRAIN);
  wire aw_fire=mem_awvalid && mem_awready;
  wire w_fire=mem_wvalid && mem_wready && mem_wlast;
  wire b_fire=mem_bvalid && mem_bready;
  wire ar_fire=mem_arvalid && mem_arready;
  wire r_fire=mem_rvalid && mem_rready && mem_rlast;
  wire quiet=(pending_aw==0 && pending_w==0 && pending_ar==0 &&
              !mem_awvalid && !mem_wvalid && !mem_arvalid && !mem_bvalid && !mem_rvalid && !tex_pending);
  wire counter_bad=(b_fire && ((!aw_fire && pending_aw==0) || (!w_fire && pending_w==0))) ||
                   (r_fire && !ar_fire && pending_ar==0) ||
                   (aw_fire && !b_fire && &pending_aw) ||
                   (w_fire && !b_fire && &pending_w) ||
                   (ar_fire && !r_fire && &pending_ar);
  assign s_axi_awready=resetn && !aw_held && !s_axi_bvalid;
  assign s_axi_wready=resetn && !w_held && !s_axi_bvalid;
  assign s_axi_arready=resetn && !s_axi_rvalid;
  assign dcr_wr_valid=resetn && state==INIT && init_cycle>=1 && init_cycle<=8;
  assign dcr_wr_addr=init_cycle==1 ? 12'd1 : init_cycle==2 ? 12'd3 : init_cycle==3 ? 12'd5 : 12'h100+{6'd0,init_cycle}-12'd4;
  assign dcr_wr_data=init_cycle==1 ? pc : init_cycle==2 ? arg_addr : init_cycle==3 ? 32'd0 :
    init_cycle==4 ? tex_base : init_cycle==5 ? tex_width : init_cycle==6 ? tex_height : init_cycle==7 ? tex_pitch : tex_mode;
  function automatic [31:0] merge_bytes(input [31:0] old_value,input [31:0] new_value,input [3:0] strb);
    integer i;
    begin
      for(i=0;i<4;i=i+1) merge_bytes[8*i+:8]=strb[i] ? new_value[8*i+:8] : old_value[8*i+:8];
    end
  endfunction
  function automatic valid_ddr(input [31:0] value);
    valid_ddr=(((value>=32'h10000000 && value<32'h30000000) || value>=32'h80000000) && value[1:0]==0);
  endfunction
  always @(posedge clk) begin
    if(!resetn) begin
      state<=IDLE; init_cycle<=0;quiet_cycles<=0;gpu_reset<=1;
      pc<=32'h10000000;arg_addr<=32'h10010000;watchdog<=100000000;
      tex_base<=0;tex_width<=0;tex_height<=0;tex_pitch<=0;tex_mode<=0;tex_bytes<=0;
      errors<=0;elapsed<=0;jobs<=0;pending_aw<=0;pending_w<=0;pending_ar<=0;
      aw_held<=0;w_held<=0;awaddr<=0;wdata<=0;wstrb<=0;
      s_axi_bvalid<=0;s_axi_bresp<=0;s_axi_rvalid<=0;s_axi_rresp<=0;s_axi_rdata<=0;
    end else begin
      case({aw_fire,b_fire})
        2'b10: pending_aw<=pending_aw+1;
        2'b01: pending_aw<=pending_aw-1;
        default: ;
      endcase
      case({w_fire,b_fire})
        2'b10: pending_w<=pending_w+1;
        2'b01: pending_w<=pending_w-1;
        default: ;
      endcase
      case({ar_fire,r_fire})
        2'b10: pending_ar<=pending_ar+1;
        2'b01: pending_ar<=pending_ar-1;
        default: ;
      endcase
      if(active) elapsed<=elapsed+1;
      case(state)
        INIT: begin
          init_cycle<=init_cycle+1;
          if(init_cycle==31) begin gpu_reset<=0;state<=WAIT_BUSY;end
        end
        WAIT_BUSY: if(gpu_busy) state<=RUN;
        RUN: if(!gpu_busy) begin state<=DRAIN;quiet_cycles<=0;end
        DRAIN: begin
          if(gpu_busy) begin state<=RUN;quiet_cycles<=0;end
          else if(quiet) begin
            if(quiet_cycles==7) begin state<=DONE;jobs<=jobs+1;end
            else quiet_cycles<=quiet_cycles+1;
          end else quiet_cycles<=0;
        end
        default: ;
      endcase
      if(s_axi_awvalid && s_axi_awready) begin aw_held<=1;awaddr<=s_axi_awaddr;end
      if(s_axi_wvalid && s_axi_wready) begin w_held<=1;wdata<=s_axi_wdata;wstrb<=s_axi_wstrb;end
      if(s_axi_bvalid && s_axi_bready) s_axi_bvalid<=0;
      if(aw_held && w_held && !s_axi_bvalid) begin
        aw_held<=0;w_held<=0;s_axi_bvalid<=1;s_axi_bresp<=0;
        case(awaddr)
          BASE+8: begin
            if(wstrb[0] && wdata[7:0]!=0) begin
              // Vortex initializes warp 0 during reset, so busy may be high
              // while held in reset. That is not an already running job.
              if(wdata[7:0]==1 && memory_ready && state==IDLE && quiet && (gpu_reset || !gpu_busy) && valid_ddr(pc) && valid_ddr(arg_addr) && tex_desc_ok) begin
                state<=INIT;init_cycle<=0;gpu_reset<=1;errors<=0;elapsed<=0;
              end else if(wdata[7:0]==2 && state==DONE) begin state<=IDLE;errors<=0;end
              else s_axi_bresp<=2;
            end
          end
          BASE+16: if(state==IDLE) pc<=merge_bytes(pc,wdata,wstrb); else s_axi_bresp<=2;
          BASE+20: if(state==IDLE) arg_addr<=merge_bytes(arg_addr,wdata,wstrb); else s_axi_bresp<=2;
          BASE+24: if(state==IDLE) watchdog<=merge_bytes(watchdog,wdata,wstrb); else s_axi_bresp<=2;
          BASE+'h40: if(state==IDLE) tex_base<=merge_bytes(tex_base,wdata,wstrb); else s_axi_bresp<=2;
          BASE+'h44: if(state==IDLE) tex_width<=merge_bytes(tex_width,wdata,wstrb); else s_axi_bresp<=2;
          BASE+'h48: if(state==IDLE) tex_height<=merge_bytes(tex_height,wdata,wstrb); else s_axi_bresp<=2;
          BASE+'h4c: if(state==IDLE) tex_pitch<=merge_bytes(tex_pitch,wdata,wstrb); else s_axi_bresp<=2;
          BASE+'h50: if(state==IDLE) tex_mode<=merge_bytes(tex_mode,wdata,wstrb); else s_axi_bresp<=2;
          BASE+'h54: if(state==IDLE) tex_bytes<=merge_bytes(tex_bytes,wdata,wstrb); else s_axi_bresp<=2;
          default: s_axi_bresp<=2;
        endcase
      end
      if(s_axi_rvalid && s_axi_rready) s_axi_rvalid<=0;
      if(s_axi_arvalid && s_axi_arready) begin
        s_axi_rvalid<=1;s_axi_rresp<=0;s_axi_rdata<=0;
        case(s_axi_araddr)
          BASE: s_axi_rdata<=32'h56580204;
          BASE+4: s_axi_rdata<=32'h00040404;
          BASE+8: s_axi_rdata<=0;
          BASE+12: s_axi_rdata<={21'd0,state,5'd0,(errors!=0),(state==DONE),active};
          BASE+16: s_axi_rdata<=pc;
          BASE+20: s_axi_rdata<=arg_addr;
          BASE+24: s_axi_rdata<=watchdog;
          BASE+28: s_axi_rdata<=errors;
          BASE+32: s_axi_rdata<=pending_aw;
          BASE+36: s_axi_rdata<=pending_ar;
          BASE+40: s_axi_rdata<=elapsed;
          BASE+44: s_axi_rdata<=jobs;
          BASE+'h30: s_axi_rdata<=32'h0000000f;
          // +0x34 bit0: synchronized DDR ready; +0x38: dual-DDR map ABI v1.
          BASE+'h34: s_axi_rdata<={31'd0,memory_ready};
          BASE+'h38: s_axi_rdata<=32'h00010001;
          BASE+'h40: s_axi_rdata<=tex_base;
          BASE+'h44: s_axi_rdata<=tex_width;
          BASE+'h48: s_axi_rdata<=tex_height;
          BASE+'h4c: s_axi_rdata<=tex_pitch;
          BASE+'h50: s_axi_rdata<=tex_mode;
          BASE+'h54: s_axi_rdata<=tex_bytes;
          BASE+'h58: s_axi_rdata<={31'd0,tex_pending};
          default: s_axi_rresp<=2;
        endcase
      end
      // Response errors survive completion. Protocol/timeout faults lock the
      // lifecycle, but deliberately do NOT reset the core or live AXI requests.
      if(b_fire && mem_bresp[1]) errors[0]<=1;
      if(mem_rvalid && mem_rready && mem_rresp[1]) errors[1]<=1;
      if(active && !memory_ready) begin errors[5]<=1;state<=FAULT;end
      if(active && elapsed>=watchdog) begin errors[2]<=1;state<=FAULT;end
      if(counter_bad) begin errors[3]<=1;state<=FAULT;end
      if(active && state!=INIT && tex_error) errors[4]<=1;
    end
  end
endmodule

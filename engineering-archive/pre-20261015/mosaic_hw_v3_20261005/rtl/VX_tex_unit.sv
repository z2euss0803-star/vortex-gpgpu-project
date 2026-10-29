`include "VX_define.vh"
// Project ISA: custom-1 / funct7=0 / funct3=0, integer u/v -> RGBA8.
module VX_tex_unit import VX_gpu_pkg::*; (
 input wire clk,reset,
 VX_dcr_bus_if.slave dcr_bus_if,
 VX_dispatch_if.slave dispatch_if,
 VX_commit_if.master commit_if,
 VX_mem_bus_if.master tex_mem_if,
 output wire tex_pending, output reg tex_error
);
 `STATIC_ASSERT (`ISSUE_WIDTH == 1 && `SIMD_WIDTH == 4 && `NUM_THREADS == 4, ("TEX integration requires c1w4t4 issue width 1"))
 localparam IDLE=0,SELECT_LANE=1,ISSUE=2,WAIT_LANE=3,FINISH=4;
 reg[2:0] state;reg[1:0] lane;
 dispatch_t meta;reg[3:0][31:0] pixels;
 reg[31:0] cfg_base,cfg_width,cfg_height,cfg_pitch,cfg_mode;
 // Like Vortex base DCRs, configuration writes must work while core reset is held.
 always @(posedge clk) if(dcr_bus_if.write_valid) case(dcr_bus_if.write_addr)
  12'h100: cfg_base<=dcr_bus_if.write_data;
  12'h101: cfg_width<=dcr_bus_if.write_data;
  12'h102: cfg_height<=dcr_bus_if.write_data;
  12'h103: cfg_pitch<=dcr_bus_if.write_data;
  12'h104: cfg_mode<=dcr_bus_if.write_data;
  default:;
 endcase
 wire sample_ready,sample_valid,sample_error;
 wire[31:0] sample_pixel;wire[25:0] line_addr;
 gpgpu_tex_sampler sampler(
  .clk(clk),.reset(reset),.cfg_base(cfg_base),.cfg_width(cfg_width),.cfg_height(cfg_height),.cfg_pitch(cfg_pitch),.cfg_mode(cfg_mode),
  .req_valid(state==ISSUE),.req_ready(sample_ready),.req_u(meta.rs1_data[lane]),.req_v(meta.rs2_data[lane]),
  .rsp_valid(sample_valid),.rsp_ready(state==WAIT_LANE),.rsp_rgba(sample_pixel),.rsp_error(sample_error),
  .mem_req_valid(tex_mem_if.req_valid),.mem_req_ready(tex_mem_if.req_ready),.mem_req_addr(line_addr),
  .mem_rsp_valid(tex_mem_if.rsp_valid),.mem_rsp_ready(tex_mem_if.rsp_ready),.mem_rsp_data(tex_mem_if.rsp_data.data)
 );
 assign tex_mem_if.req_data.rw=1'b0;
 assign tex_mem_if.req_data.addr=line_addr;
 assign tex_mem_if.req_data.data='0;
 assign tex_mem_if.req_data.byteen='1;
 assign tex_mem_if.req_data.flags='0;
 assign tex_mem_if.req_data.tag='0; // One line outstanding; arbiter adds source tag.
 assign dispatch_if.ready=!reset && state==IDLE;
 assign commit_if.valid=!reset && state==FINISH;
 assign tex_pending=(state!=IDLE) || dispatch_if.valid;
 always @(*) begin
  commit_if.data='0;
  commit_if.data.uuid=meta.uuid;commit_if.data.wid=NW_WIDTH'(meta.wis);
  commit_if.data.sid=meta.sid;commit_if.data.tmask=meta.tmask;commit_if.data.PC=meta.PC;
  commit_if.data.wb=meta.wb;commit_if.data.rd=meta.rd;commit_if.data.data=pixels;
  commit_if.data.sop=meta.sop;commit_if.data.eop=meta.eop;
 end
 always @(posedge clk) begin
  if(reset) begin state<=IDLE;lane<=0;meta<='0;pixels<='0;tex_error<=0;end
  else case(state)
   IDLE: if(dispatch_if.valid) begin meta<=dispatch_if.data;pixels<='0;lane<=0;state<=SELECT_LANE;end
   SELECT_LANE: if(meta.tmask[lane]) state<=ISSUE;
                else if(lane==3) state<=FINISH;
                else lane<=lane+1'b1;
   ISSUE: if(sample_ready) state<=WAIT_LANE;
   WAIT_LANE: if(sample_valid) begin
    pixels[lane]<=sample_pixel;if(sample_error) tex_error<=1;
    if(lane==3) state<=FINISH;else begin lane<=lane+1'b1;state<=SELECT_LANE;end
   end
   FINISH: if(commit_if.ready) state<=IDLE;
   default: state<=IDLE;
  endcase
 end
endmodule

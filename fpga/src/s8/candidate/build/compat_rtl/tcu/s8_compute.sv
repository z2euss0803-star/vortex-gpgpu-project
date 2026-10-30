`timescale 1ns/1ps
// Standalone research interface. This module is not wired into the GPU.
module s8_compute #(
 parameter integer PIPELINED=1
)(
 input wire clk,reset,
 input wire cmd_valid,
 output wire cmd_ready,
 input wire [12:0] k_count,
 input wire step_valid,
 output wire step_ready,
 input wire [127:0] a_flat,b_flat,
 output reg result_valid,
 input wire result_ready,
 output wire [3071:0] c_flat,
 output reg cmd_error
);
reg busy;
reg [12:0] target,accepted;
reg v1,v2,last1,last2;
wire good_k=(k_count>=13'd1)&&(k_count<=13'd4096);
assign cmd_ready=!reset && !busy && !result_valid;
assign step_ready=!reset && busy && (accepted<target);
wire cmd_fire=cmd_valid&&cmd_ready;
wire start=cmd_fire&&good_k;
wire step_fire=step_valid&&step_ready;
wire last_step=step_fire&&(accepted==target-13'd1);
wire done=PIPELINED ? (v2&&last2) : last_step;
always @(posedge clk)begin
 if(reset)begin
  busy<=0;target<=0;accepted<=0;result_valid<=0;cmd_error<=0;
  v1<=0;v2<=0;last1<=0;last2<=0;
 end else begin
  cmd_error<=cmd_fire&&!good_k;
  v1<=step_fire;v2<=v1;last1<=last_step;last2<=last1;
  if(step_fire)accepted<=accepted+13'd1;
  if(start)begin busy<=1;target<=k_count;accepted<=0;end
  if(done)begin busy<=0;result_valid<=1;end
  if(result_valid&&result_ready)result_valid<=0;
 end
end
genvar i,j;
generate for(i=0;i<8;i=i+1)begin:rows
 for(j=0;j<8;j=j+1)begin:columns
  wire signed [15:0] ai=$signed(a_flat[i*16+:16]);
  wire signed [15:0] bi=$signed(b_flat[j*16+:16]);
  if(PIPELINED)begin:pipelined
   wire [47:0] p;
   // X=multiplier, Y=multiplier, Z=P feedback, W=zero: P <- M + P.
   // A/B, M and P enables track the three accepted-step pipeline stages.
   DSP48E2 #(
    .AREG(1),.ACASCREG(1),.BREG(1),.BCASCREG(1),.MREG(1),.PREG(1),
    .ADREG(0),.CREG(0),.DREG(0),.INMODEREG(0),.OPMODEREG(0),
    .ALUMODEREG(0),.CARRYINREG(0),.CARRYINSELREG(0),
    .AMULTSEL("A"),.BMULTSEL("B"),.A_INPUT("DIRECT"),.B_INPUT("DIRECT"),
    .USE_MULT("MULTIPLY"),.USE_SIMD("ONE48"),.USE_PATTERN_DETECT("NO_PATDET")
   ) mac(
    .CLK(clk),.A({{14{ai[15]}},ai}),.B({{2{bi[15]}},bi}),
    .C(48'd0),.D(27'd0),.ACIN(30'd0),.BCIN(18'd0),.PCIN(48'd0),
    .ALUMODE(4'b0000),.INMODE(5'b00000),.OPMODE(9'b000100101),
    .CARRYIN(1'b0),.CARRYINSEL(3'd0),.CARRYCASCIN(1'b0),.MULTSIGNIN(1'b0),
    .CEA1(step_fire||reset),.CEA2(step_fire||reset),
    .CEB1(step_fire||reset),.CEB2(step_fire||reset),
    .CEM(v1||reset),.CEP(v2||start||reset),
    .CEAD(1'b0),.CED(1'b0),.CEC(1'b0),.CECARRYIN(1'b0),
    .CEALUMODE(1'b0),.CECTRL(1'b0),.CEINMODE(1'b0),
    .RSTA(reset),.RSTB(reset),.RSTM(reset),.RSTP(reset||start),
    .RSTC(reset),.RSTD(reset),.RSTALUMODE(reset),.RSTCTRL(reset),
    .RSTINMODE(reset),.RSTALLCARRYIN(reset),.P(p),
    .ACOUT(),.BCOUT(),.PCOUT(),.CARRYCASCOUT(),.CARRYOUT(),
    .MULTSIGNOUT(),.OVERFLOW(),.UNDERFLOW(),.PATTERNDETECT(),.PATTERNBDETECT(),.XOROUT()
   );
   assign c_flat[(i*8+j)*48+:48]=p;
  end else begin:direct
   (* use_dsp="yes" *) reg signed [47:0] acc;
   (* use_dsp="yes" *) wire signed [31:0] direct_product=ai*bi;
   always @(posedge clk)begin
    if(reset||start)acc<=48'sd0;
    else if(step_fire)acc<=acc+{{16{direct_product[31]}},direct_product};
   end
   assign c_flat[(i*8+j)*48+:48]=acc;
  end
 end
end endgenerate
endmodule

// Copyright © 2019-2023; Apache-2.0. Original body is VX_tcu_int_legacy.sv.
`include "VX_define.vh"
module VX_tcu_int import VX_gpu_pkg::*, VX_tcu_pkg::*; #(
 parameter `STRING INSTANCE_ID=""
)(
 `SCOPE_IO_DECL
 input wire clk,reset,
 VX_execute_if.slave execute_if,
 VX_result_if.master result_if
);
 if(TCU_NATIVE_ENABLE)begin:g_native
  VX_execute_if #(.data_t(tcu_exe_t)) legacy_ex(), native_ex();
  VX_result_if #(.data_t(tcu_res_t)) legacy_res(), native_res();
  wire native_request=(execute_if.data.op_args.tcu.fmt_s>=TCU_S8_BEGIN_ID)
                     &&(execute_if.data.op_args.tcu.fmt_s<=TCU_S8_READ_ID);
  assign legacy_ex.valid=execute_if.valid&&!native_request;
  assign native_ex.valid=execute_if.valid&&native_request;
  assign legacy_ex.data=execute_if.data;
  assign native_ex.data=execute_if.data;
  assign execute_if.ready=native_request?native_ex.ready:legacy_ex.ready;
  VX_tcu_int_legacy #(.INSTANCE_ID(INSTANCE_ID)) legacy (
`ifdef SCOPE
   .scope_reset(scope_reset),.scope_bus_in(scope_bus_in),.scope_bus_out(scope_bus_out),
`endif
   .clk(clk),.reset(reset),.execute_if(legacy_ex),.result_if(legacy_res));
  VX_tcu_native_s8 native_core(.clk(clk),.reset(reset),.execute_if(native_ex),.result_if(native_res));
  // No extra legacy stage. Freeze a grant during stalls, alternate on capture.
  reg prefer_native,locked,locked_native;
  wire choose_native=locked?locked_native:(native_res.valid&&(!legacy_res.valid||prefer_native));
  assign result_if.valid=choose_native?native_res.valid:legacy_res.valid;
  assign result_if.data=choose_native?native_res.data:legacy_res.data;
  assign legacy_res.ready=result_if.ready&&!choose_native;
  assign native_res.ready=result_if.ready&&choose_native;
  always @(posedge clk)begin
   if(reset)begin prefer_native<=0;locked<=0;locked_native<=0;end
   else if(result_if.valid)begin
    if(result_if.ready)begin locked<=0;prefer_native<=!choose_native;end
    else if(!locked)begin locked<=1;locked_native<=choose_native;end
   end
  end
 end else begin:g_legacy
  VX_tcu_int_legacy #(.INSTANCE_ID(INSTANCE_ID)) legacy (
`ifdef SCOPE
   .scope_reset(scope_reset),.scope_bus_in(scope_bus_in),.scope_bus_out(scope_bus_out),
`endif
   .clk(clk),.reset(reset),.execute_if(execute_if),.result_if(result_if));
 end
endmodule

`include "VX_define.vh"
// Single owner register-stream adapter, without a second accumulator bank.
module VX_tcu_native_s8 import VX_gpu_pkg::*,VX_tcu_pkg::*; (
 input wire clk,reset,
 VX_execute_if.slave execute_if,
 VX_result_if.master result_if
);
 reg active;
 reg [NW_WIDTH-1:0] owner;
 reg [12:0] target,accepted;
 reg [3:0] status_code[`NUM_WARPS];
 reg [23:0] captured_pages;
 reg response_valid,response_page;
 reg [4:0] response_page_index;
 tcu_res_t response;
 wire cmd_ready,step_ready,result_valid,cmd_error;
 wire [3071:0] c_flat;
 wire [3:0] fmt=execute_if.data.op_args.tcu.fmt_s;
 wire is_begin=fmt==TCU_S8_BEGIN_ID;
 wire is_step=fmt==TCU_S8_STEP_ID;
 wire is_read=fmt==TCU_S8_READ_ID;
 wire [NW_WIDTH-1:0] wid=execute_if.data.wid;
 wire owned=active&&(owner==wid);
 wire [31:0] control=execute_if.data.rs3_data[0];
 wire [4:0] page=control[4:0];
 wire releasing=is_read&&page==31&&control[8];
 wire [12:0] request_k=control[12:0];
 wire ctrl_other_lanes=(execute_if.data.rs3_data[3:1]!=0);
 reg [3:0] error;
 always @(*)begin
  error=S8_OK;
  if(execute_if.data.tmask!=4'hf)error=S8_BAD_MASK;
  else if(execute_if.data.op_args.tcu.fmt_d!=TCU_I32_ID
          ||execute_if.data.pid!=0||!execute_if.data.sop||!execute_if.data.eop
          ||execute_if.data.op_args.tcu.step_m!=0||execute_if.data.op_args.tcu.step_n!=0)error=S8_BAD_CONTROL;
  else if(is_begin)begin
   if(ctrl_other_lanes||control[31:13]!=0)error=S8_BAD_CONTROL;
   else if(request_k==0||request_k>4096)error=S8_BAD_K;
   else if(active||!cmd_ready)error=S8_BUSY;
  end else if(is_step)begin
   if(!owned)error=S8_OWNER;
   else if(accepted>=target)error=S8_EXCESS_STEP;
   else if(!step_ready)error=S8_NOT_READY;
  end else if(is_read)begin
   if(ctrl_other_lanes||(control&32'hfffffee0)!=0||(control[8]&&page!=31))error=S8_BAD_CONTROL;
   else if(page>=24&&page!=31)error=S8_BAD_PAGE;
   else if(page==31)begin
    if(releasing)begin
     if(!owned)error=S8_OWNER;
     else if(!result_valid||captured_pages!=24'hffffff)error=S8_INCOMPLETE;
    end
   end else if(!owned)error=S8_OWNER;
   else if(!result_valid)error=S8_NOT_READY;
  end else error=S8_BAD_CONTROL;
 end
 assign execute_if.ready=!reset&&(!response_valid||result_if.ready);
 wire request_fire=execute_if.valid&&execute_if.ready;
 wire cmd_valid=request_fire&&is_begin&&error==S8_OK;
 wire step_valid=request_fire&&is_step&&error==S8_OK;
 wire release_fire=request_fire&&releasing&&error==S8_OK;
 s8_compute core(.clk(clk),.reset(reset),.cmd_valid(cmd_valid),.cmd_ready(cmd_ready),.k_count(request_k),
  .step_valid(step_valid),.step_ready(step_ready),.a_flat(execute_if.data.rs1_data),.b_flat(execute_if.data.rs2_data),
  .result_valid(result_valid),.result_ready(release_fire),.c_flat(c_flat),.cmd_error(cmd_error));
 `UNUSED_VAR(cmd_error)
 function automatic [127:0] make_status(input [3:0] code,input [NW_WIDTH-1:0] requester);
  reg [127:0] value;
  begin
   value='0;value[31:0]=32'(code);
   if(active&&requester==owner)begin
    value[63:32]=32'(accepted);value[95:64]=32'(target);
    value[96]=active;value[97]=result_valid;value[98]=(captured_pages==24'hffffff);
   end
   make_status=value;
  end
 endfunction
 reg [127:0] data_next;
 reg [3:0] queried_status;
 always @(*)begin
  queried_status=status_code[wid];
  if(owned&&queried_status==S8_OK&&!result_valid)queried_status=S8_NOT_READY;
  data_next='0;
  if(is_begin)begin
   data_next=make_status(error,wid);
   // Successful allocation reports the context created by this request.
   if(error==S8_OK)begin
    data_next='0;data_next[95:64]=32'(request_k);data_next[96]=1;
   end
  end
  else if(is_read&&page==31) data_next=make_status(error!=S8_OK?error:(releasing?4'(S8_OK):queried_status),wid);
  else if(is_read&&page<24&&error==S8_OK) data_next=c_flat[page*128+:128];
 end
 assign result_if.valid=response_valid;
 assign result_if.data=response;
 always @(posedge clk)begin
  if(reset)begin
   active<=0;owner<=0;target<=0;accepted<=0;captured_pages<=0;
   response_valid<=0;response_page<=0;response_page_index<=0;response<='0;
   for(integer w=0;w<`NUM_WARPS;w=w+1)status_code[w]<=S8_OK;
  end else begin
   if(response_valid&&result_if.ready)begin
    response_valid<=0;
    if(response_page)captured_pages[response_page_index]<=1;
   end
   if(request_fire)begin
    response_valid<=1;
    response.uuid<=execute_if.data.uuid;response.wid<=wid;
    response.tmask<=execute_if.data.tmask;response.PC<=execute_if.data.PC;
    response.wb<=!is_step;response.rd<=execute_if.data.rd;
    response.pid<=execute_if.data.pid;response.sop<=execute_if.data.sop;response.eop<=execute_if.data.eop;
    response.data<=data_next;
    response_page<=is_read&&page<24&&error==S8_OK;
    response_page_index<=page;
    if(error!=S8_OK)status_code[wid]<=error;
    if(cmd_valid)begin
     active<=1;owner<=wid;target<=request_k;accepted<=0;captured_pages<=0;status_code[wid]<=S8_OK;
    end
    if(step_valid)accepted<=accepted+13'd1;
    if(release_fire)begin active<=0;captured_pages<=0;end
   end
  end
 end
endmodule

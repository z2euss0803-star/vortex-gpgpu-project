`timescale 1ns/1ps
module axi_memory #(parameter IDW=6)(
 input logic clk,reset,
 input logic awvalid,output logic awready,input logic[31:0]awaddr,input logic[IDW-1:0]awid,input logic[7:0]awlen,
 input logic wvalid,output logic wready,input logic[511:0]wdata,input logic[63:0]wstrb,input logic wlast,
 output logic bvalid,input logic bready,output logic[IDW-1:0]bid,output logic[1:0]bresp,
 input logic arvalid,output logic arready,input logic[31:0]araddr,input logic[IDW-1:0]arid,input logic[7:0]arlen,
 output logic rvalid,input logic rready,output logic[511:0]rdata,output logic[IDW-1:0]rid,output logic[1:0]rresp,output logic rlast);
 byte unsigned bytes[longint unsigned];
 logic write_active=0;logic[31:0]write_address;logic[IDW-1:0]write_id;
 integer cycle=0,packed_writes=0;integer fault=0,stall=0,read_delay=0;
 logic read_pending=0,fault_block=0;
 integer load_fd,load_rc;reg[63:0]load_addr;reg[7:0]load_byte;string init_path;
 initial begin
  if(!$value$plusargs("MEM=%s",init_path))$fatal(1,"MEM_ARGUMENT");
  void'($value$plusargs("FAULT=%d",fault));void'($value$plusargs("STALL=%d",stall));
  load_fd=$fopen(init_path,"r");if(!load_fd)$fatal(1,"MEM_OPEN");
  while(!$feof(load_fd))begin
   load_rc=$fscanf(load_fd,"%h %h\n",load_addr,load_byte);
   if(load_rc==2)bytes[load_addr]=load_byte;else if(!$feof(load_fd))$fatal(1,"MEM_PARSE");
  end
  $fclose(load_fd);
 end
 function automatic logic[31:0] read32(input longint unsigned a);
  for(int i=0;i<4;i++)read32[i*8+:8]=bytes.exists(a+i)?bytes[a+i]:0;
 endfunction
 task load_overlay(input string path);
  integer f,rc;reg[63:0]a;reg[7:0]b;
  f=$fopen(path,"r");if(!f)$fatal(1,"OVERLAY_OPEN");
  while(!$feof(f))begin rc=$fscanf(f,"%h %h\n",a,b);
   if(rc==2)bytes[a]=b;else if(!$feof(f))$fatal(1,"OVERLAY_PARSE");end
  $fclose(f);
 endtask
 assign awready=!reset&&!write_active&&!bvalid&&(!stall||cycle%7!=0);
 assign wready=!reset&&write_active&&!bvalid&&(!stall||cycle%5!=0);
 assign arready=!reset&&!rvalid&&!read_pending&&(!stall||cycle%11!=0);
 always @(posedge clk)begin
  cycle<=cycle+1;
  if(reset)begin write_active<=0;bvalid<=0;rvalid<=0;packed_writes<=0;read_pending<=0;fault_block<=0;end
  else begin
   if(bvalid&&bready)bvalid<=0;
   if(rvalid&&rready)rvalid<=0;
   if(read_pending&&!fault_block)begin
    if(read_delay==0)begin rvalid<=1;read_pending<=0;end
    else read_delay<=read_delay-1;
   end
   if(awvalid&&awready)begin
    if(awlen!=0)$fatal(1,"UNEXPECTED_WRITE_BURST");
    write_address<=awaddr;write_id<=awid;write_active<=1;
   end
   if(wvalid&&wready)begin
    if(!wlast)$fatal(1,"WRITE_LAST");
    for(int i=0;i<64;i++)if(wstrb[i])bytes[longint'(write_address)+i]=wdata[i*8+:8];
    if(write_address>=32'h86c4df40&&write_address<32'h86d0df40)packed_writes<=packed_writes+$countones(wstrb);
    write_active<=0;bvalid<=1;bid<=write_id;bresp<=0;
   end
   if(arvalid&&arready)begin
    if(arlen!=0)$fatal(1,"UNEXPECTED_READ_BURST");
    for(int i=0;i<64;i++)rdata[i*8+:8]<=bytes.exists(longint'(araddr)+i)?bytes[longint'(araddr)+i]:0;
    rid<=arid;rlast<=1;read_pending<=1;read_delay<=stall ? 1+(arid%4)*3 : 0;rresp<=0;
    if(fault&&araddr>=32'h86c4df40&&araddr<32'h86d0df40)begin
     fault_block<=1;$display("FUSED_MEMORY_FAULT_INJECTED");
    end
   end
  end
 end
endmodule

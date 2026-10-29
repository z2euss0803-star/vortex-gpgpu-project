reg[31:0] gold_ext[0:575];
integer ext_case,ext_i,ext_core,ext_mode,tex_calls=0,tcu_calls=0,tex_before,tcu_before;
integer ext_bank=0;
integer tests_completed=0,sample_calls=0,sample_before,start_cycle;
always @(posedge clk) if(resetn && !dut.gpu_reset) begin
 if(dut.gpu.vortex.g_clusters[0].cluster.g_sockets[0].socket.g_cores[0].core.execute.tex_unit.sampler.req_valid &&
    dut.gpu.vortex.g_clusters[0].cluster.g_sockets[0].socket.g_cores[0].core.execute.tex_unit.sampler.req_ready) sample_calls<=sample_calls+1;
 if(dut.gpu.vortex.g_clusters[0].cluster.g_sockets[0].socket.g_cores[0].core.execute.tex_unit.dispatch_if.valid &&
    dut.gpu.vortex.g_clusters[0].cluster.g_sockets[0].socket.g_cores[0].core.execute.tex_unit.dispatch_if.ready) tex_calls<=tex_calls+1;
 if(dut.gpu.vortex.g_clusters[0].cluster.g_sockets[0].socket.g_cores[0].core.execute.tcu_unit.dispatch_if[0].valid &&
    dut.gpu.vortex.g_clusters[0].cluster.g_sockets[0].socket.g_cores[0].core.execute.tcu_unit.dispatch_if[0].ready) tcu_calls<=tcu_calls+1;
end
task automatic run_extended_tests;
 begin
  for(ext_case=0;ext_case<10;ext_case=ext_case+1) begin
   $readmemh($sformatf("image_%0d.hex",ext_case),memory,0,65535);
   $readmemh($sformatf("gold_%0d.hex",ext_case),gold_ext);
   for(ext_core=0;ext_core<4;ext_core=ext_core+1)begin
     for(ext_i=0;ext_i<128;ext_i=ext_i+1)
       memory['h21000/4+ext_core*128+ext_i]=32'hdeadbeef;
     for(ext_i=0;ext_i<16;ext_i=ext_i+1)
       memory['h22000/4+ext_core*16+ext_i]=32'hdeadbeef;
   end
   for(ext_i=0;ext_i<16;ext_i=ext_i+1)begin
     memory['h21800/4+ext_i]=32'haa55aa55;
     memory['h22100/4+ext_i]=32'h87654321;
   end
   ext_bank=(ext_case%2 ? 65536 : 0);
   if(ext_bank!=0)begin
    for(ext_i=0;ext_i<65536;ext_i=ext_i+1)memory[65536+ext_i]=memory[ext_i];
    memory[65537]=32'h80000337;
    for(ext_i=0;ext_i<6;ext_i=ext_i+1)memory[65536+'h10000/4+ext_i]=memory['h10000/4+ext_i]+32'h70000000;
   end
   wr(16,ext_bank ? 32'h80000000 : 32'h10000000);
   wr(20,ext_bank ? 32'h80010000 : 32'h10010000);
   ext_mode=ext_case==9 ? 0 : 8+(ext_case%8);
   wr('h40,ext_bank ? 32'h80024000 : 32'h10024000);wr('h44,7);wr('h48,5);wr('h4c,32);wr('h50,ext_mode);wr('h54,160);
   tex_before=tex_calls;tcu_before=tcu_calls;sample_before=sample_calls;start_cycle=cycle;
   wr(8,1);value=0;
   while(!value[1]) begin
    repeat(30)@(negedge clk);rd(12);
    if(cycle-start_cycle>50000)$fatal(1,"native case timeout case=%0d",ext_case);
    if(value[2] && ext_case!=9)begin rd(28);$fatal(1,"extended case %0d error %h",ext_case,value);end
   end
   rd(28);if(value!==(ext_case==9 ? 32'd16 : 32'd0))$fatal(1,"wrong extended error %h",value);
   for(ext_core=0;ext_core<4;ext_core=ext_core+1)begin
    for(ext_i=0;ext_i<128;ext_i=ext_i+1)
     if(memory[ext_bank+'h21000/4+ext_core*128+ext_i]!==gold_ext[ext_core*128+ext_i])
       $fatal(1,"TCU case%0d core%0d out%0d got%h want%h",ext_case,ext_core,ext_i,memory[ext_bank+'h21000/4+ext_core*128+ext_i],gold_ext[ext_core*128+ext_i]);
    for(ext_i=0;ext_i<16;ext_i=ext_i+1)
     if(memory[ext_bank+'h22000/4+ext_core*16+ext_i]!==gold_ext[512+ext_core*16+ext_i])
       $fatal(1,"TEX case%0d core%0d thread%0d got%h want%h",ext_case,ext_core,ext_i,memory[ext_bank+'h22000/4+ext_core*16+ext_i],gold_ext[512+ext_core*16+ext_i]);
   end
   for(ext_i=0;ext_i<16;ext_i=ext_i+1)
    if(memory[ext_bank+'h20fc0/4+ext_i]!==32'h55aa55aa || memory[ext_bank+'h21800/4+ext_i]!==32'haa55aa55 ||
       memory[ext_bank+'h21fc0/4+ext_i]!==32'h12345678 || memory[ext_bank+'h22100/4+ext_i]!==32'h87654321)
       $fatal(1,"extension guard damaged i=%0d",ext_i);
   if(tex_calls-tex_before!=4 || tcu_calls-tcu_before!=64)$fatal(1,"native dispatch coverage TEX=%0d TCU=%0d",tex_calls-tex_before,tcu_calls-tcu_before);
   if(sample_calls-sample_before!=(ext_case==8?8:16))$fatal(1,"masked lanes issued TEX samples");
   rd('h20);if(value!=0)$fatal(1,"pending write at extension DONE");
   rd('h24);if(value!=0)$fatal(1,"pending read at extension DONE");
   rd('h58);if(value!=0)$fatal(1,"TEX pending at DONE");
   $display("NATIVE_TCU_TEX_CASE_PASS case=%0d mode=%0d TCU512 TEX64 calls=%0d/%0d samples=%0d",ext_case,ext_mode,tcu_calls-tcu_before,tex_calls-tex_before,sample_calls-sample_before);
   tests_completed=tests_completed+1;wr(8,2);
  end
  $display("NATIVE_TCU_TEX_RTL_PASS: 10 cases 5120 matrix elements 640 texels actual Xilinx floating IP");
 end
endtask

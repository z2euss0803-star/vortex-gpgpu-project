"""Independent integer oracle + randomized memory/response stalls."""
from pathlib import Path
import random,subprocess,sys
r=Path(__file__).resolve().parents[1];o=r/'evidence/texture';o.mkdir(parents=True,exist_ok=True)
rng=random.Random(261926)
mem=[rng.getrandbits(32) for _ in range(4096)]
(o/'memory.hex').write_text('\n'.join(f'{x:08x}' for x in mem)+'\n')
def sample(u,v,w,h,p,mode):
    if not mode&8:return 0
    x=u//65536;y=v//65536
    if not mode&1:x=(u+32768)//65536;y=(v+32768)//65536
    def at(a,b):
        a=a%w if mode&2 else max(0,min(w-1,a))
        b=b%h if mode&4 else max(0,min(h-1,b))
        return mem[b*(p//4)+a]
    if not mode&1:return at(x,y)
    fx=(u%65536)//256;fy=(v%65536)//256
    weights=[(256-fx)*(256-fy),fx*(256-fy),(256-fx)*fy,fx*fy]
    pix=[at(x,y),at(x+1,y),at(x,y+1),at(x+1,y+1)]
    return sum(((sum(((q>>(8*c))&255)*wt for q,wt in zip(pix,weights))+32768)//65536)<<(8*c) for c in range(4))
rows=[]
for w,h,p in [(1,1,4),(3,5,20),(17,9,80),(63,61,256),(4096,1,16384),(1,4096,4)]:
    for mode in range(8,16):
        for k in range(24):
            u,v=([(-32768,-32768),(-1,-65537),(0,0),(32768,32768),(2147483647,-2147483648)][k] if k<5 else (rng.randrange(-2**31,2**31),rng.randrange(-2**31,2**31)))
            rows.append((w,h,p,mode,u&0xffffffff,v&0xffffffff,sample(u,v,w,h,p,mode)))
rows.append((3,5,20,0,0,0,0))
(o/'cases.hex').write_text('\n'.join(''.join(f'{x:08x}' for x in row) for row in rows)+'\n')
rtl=r/'rtl/gpgpu_tex_sampler.sv'
assert rtl.exists(),'Texture sampler RTL missing: no implemented sampling behavior'
tb='''`timescale 1ns/1ps
module tb;
reg clk=0;always #5 clk=~clk;reg reset=1;
reg [31:0] cfg_base=32'h10040000,cfg_width,cfg_height,cfg_pitch,cfg_mode;
reg req_valid=0;wire req_ready;reg[31:0] req_u,req_v;
wire rsp_valid;reg rsp_ready=0;wire[31:0] rsp_rgba;wire rsp_error;
wire mem_req_valid;reg mem_req_ready=0;wire[25:0] mem_req_addr;
reg mem_rsp_valid=0;wire mem_rsp_ready;reg[511:0] mem_rsp_data;
gpgpu_tex_sampler dut(.*);
reg[31:0] memory[0:4095];reg[223:0] cases[0:CASES-1];
integer cyc=0,i,j,delay_count=0,read_index,reads=0;reg pending=0;
reg[25:0] saved_addr;reg[31:0] saved_pixel;integer reads_before;
task automatic fetch_check(input[31:0] expected_pixel);
 begin
  @(negedge clk);req_valid=1;
  do @(posedge clk);while(!req_ready);
  @(negedge clk);req_valid=0;
  wait(rsp_valid);#1;
  if(rsp_rgba!==expected_pixel || rsp_error)$fatal(1,"cache ownership mismatch");
  @(negedge clk);rsp_ready=1;@(negedge clk);rsp_ready=0;
 end
endtask
always @(posedge clk) begin
 cyc<=cyc+1;mem_req_ready<=!pending && !mem_rsp_valid && (cyc%5!=0);
 if(mem_req_valid && mem_req_ready) begin
  saved_addr<=mem_req_addr;pending<=1;delay_count<=2+cyc%7;reads<=reads+1;
 end
 if(pending && delay_count>0) delay_count<=delay_count-1;
 if(pending && delay_count==0) begin
  read_index=(saved_addr-26'h0401000)*16;
  if(read_index<0 || read_index+15>=4096) $fatal(1,"bad memory line %h",saved_addr);
  for(j=0;j<16;j=j+1) mem_rsp_data[32*j+:32]<=memory[read_index+j];
  mem_rsp_valid<=1;pending<=0;
 end
 if(mem_rsp_valid && mem_rsp_ready) mem_rsp_valid<=0;
end
initial begin
 $dumpfile("texture.vcd");$dumpvars(1,tb);$readmemh("memory.hex",memory);$readmemh("cases.hex",cases);
 for(i=0;i<CASES;i=i+1) begin
  @(negedge clk);reset=1;req_valid=0;rsp_ready=0;
  repeat(3) @(negedge clk);
  {cfg_width,cfg_height,cfg_pitch,cfg_mode,req_u,req_v,saved_pixel}=cases[i];
  reset=0;req_valid=1;
  do @(posedge clk);while(!req_ready);
  @(negedge clk);req_valid=0;
  wait(rsp_valid);#1;
  if(rsp_rgba!==saved_pixel || rsp_error!==!cfg_mode[3]) $fatal(1,"case %0d got %h expected %h err %b",i,rsp_rgba,saved_pixel,rsp_error);
  repeat(4) begin @(posedge clk);#1;if(!rsp_valid || rsp_rgba!==saved_pixel) $fatal(1,"response lost under stall");end
  @(negedge clk);rsp_ready=1;@(negedge clk);rsp_ready=0;
 end
 @(negedge clk);reset=1;repeat(3)@(negedge clk);
 cfg_width=1;cfg_height=1;cfg_pitch=4;cfg_mode=8;req_u=0;req_v=0;reset=0;
 saved_pixel=memory[0];fetch_check(saved_pixel);reads_before=reads;
 memory[0]=~saved_pixel;
 fetch_check(saved_pixel);
 if(reads!=reads_before)$fatal(1,"repeat sample should hit cache");
 @(negedge clk);reset=1;repeat(3)@(negedge clk);reset=0;
 fetch_check(~saved_pixel);
 if(reads!=reads_before+1)$fatal(1,"job reset must invalidate cached texture");
 $display("TEXTURE_UNIT_PASS cases=%0d reads=%0d cache_hit_and_reset_refetch=PASS",CASES,reads);$finish;
end
initial begin #100000000;$fatal(1,"texture timeout");end
endmodule
'''.replace('CASES',str(len(rows)))
(o/'tb.sv').write_text(tb)
for name,cmd in [('syntax',[r'C:\iverilog\bin\iverilog.exe','-g2012','-t','null',str(rtl),'tb.sv']),('compile',[r'C:\iverilog\bin\iverilog.exe','-g2012','-o','sim.vvp',str(rtl),'tb.sv']),('run',[r'C:\iverilog\bin\vvp.exe','sim.vvp'])]:
    p=subprocess.run(cmd,cwd=o,capture_output=True,text=True,timeout=120);s=p.stdout+p.stderr;(o/(name+'.log')).write_text(s);print(s,end='')
    if p.returncode or 'FATAL:' in s:sys.exit(1)

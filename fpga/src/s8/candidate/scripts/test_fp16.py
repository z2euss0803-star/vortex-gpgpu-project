"""Exhaust the actual converter against Python's IEEE half conversion."""
from pathlib import Path
import struct, subprocess, sys
r=Path(__file__).resolve().parents[1]
mode=sys.argv[1] if len(sys.argv)>1 else 'fixed'
src=r/('vendor/vortex/hw/rtl' if mode=='original' else 'build/compat_rtl')/'tcu/VX_tcu_fedp_dsp.sv'
out=r/'evidence'/('fp16_'+mode);out.mkdir(parents=True,exist_ok=True)
s=src.read_text();s=s[s.index('module fp16_to_fp32'):s.index('// Module to convert BF16')]
(out/'converter.sv').write_text("`define UNUSED_VAR(x) wire unused = &{1'b0,x}\n"+s)
gold=[]
for i in range(65536):
    f=struct.unpack('<e',struct.pack('<H',i))[0]
    gold.append(f'{struct.unpack("<I",struct.pack("<f",f))[0]:08x}')
(out/'gold.hex').write_text('\n'.join(gold)+'\n')
(out/'tb.sv').write_text('''`timescale 1ns/1ps
module tb;
reg [15:0] x;wire[31:0] y;reg[31:0] gold[0:65535];integer i,errors;
fp16_to_fp32 dut(x,y);
initial begin
 $dumpfile("converter.vcd");$dumpvars(0,dut);$readmemh("gold.hex",gold);errors=0;
 for(i=0;i<65536;i=i+1) begin
  x=i;#1;
  if(x[14:10]==31 && x[9:0]!=0) begin
   if(y[30:23]!==255 || y[22:0]==0) errors=errors+1;
  end else if(y!==gold[i]) begin
   if(errors<4) $display("MISMATCH %h -> %h expected %h",x,y,gold[i]);
   errors=errors+1;
  end
 end
 $display("CONVERTER_ERRORS=%0d",errors);
 if(errors) $fatal(1,"converter failed");
 $display("FP16_EXHAUSTIVE_PASS 65536");$finish;
end
endmodule
''')
for name,cmd in [('syntax',[r'C:\iverilog\bin\iverilog.exe','-g2012','-t','null','converter.sv','tb.sv']),('compile',[r'C:\iverilog\bin\iverilog.exe','-g2012','-o','sim.vvp','converter.sv','tb.sv']),('run',[r'C:\iverilog\bin\vvp.exe','sim.vvp'])]:
    p=subprocess.run(cmd,cwd=out,capture_output=True,text=True);log=p.stdout+p.stderr
    (out/(name+'.log')).write_text(log);print(log,end='')
    if p.returncode or 'FATAL:' in log:sys.exit(1)

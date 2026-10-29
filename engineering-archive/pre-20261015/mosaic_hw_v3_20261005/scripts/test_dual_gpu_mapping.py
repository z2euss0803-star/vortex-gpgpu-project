"""Compile the exact address assignments extracted from the production wrapper.
This checks combinational mapping boundaries only, not full GPU elaboration.
The actual four-core integration is independently exercised by sim_gpu.py.
"""
from pathlib import Path
import re, subprocess, json, hashlib
r=Path(__file__).resolve().parents[1]; out=r/'evidence/dual_mapping';out.mkdir(parents=True,exist_ok=True)
s=(r/'rtl/gpgpu_subsystem.sv').read_text()
assert '.AXI_DATA_WIDTH(512)' in s
assert re.search(r'\[511:0\] HP_rdata',s) and re.search(r'\[63:0\] HP_wstrb',s)
assignments='\n'.join(re.findall(r'assign HP_(?:ar|aw)addr=.*?;',s))
assert len(assignments.splitlines())==2
sv='''`timescale 1ns/1ps
module mapping;
reg[31:0] gpu_araddr[1],gpu_awaddr[1];wire[48:0] HP_araddr,HP_awaddr;
'''+assignments+'''
task check(input[31:0] gpu,input[48:0] expected);
begin gpu_araddr[0]=gpu;gpu_awaddr[0]=gpu;#1;
if(HP_araddr!==expected || HP_awaddr!==expected)$fatal(1,"mapping gpu=%h ar=%h aw=%h expected=%h",gpu,HP_araddr,HP_awaddr,expected);
end endtask
initial begin
check(32'h00000000,49'h000000000);
check(32'h10000000,49'h010000000);
check(32'h2ffffffc,49'h02ffffffc);
check(32'h7fffffff,49'h07fffffff);
check(32'h80000000,49'h500000000);
check(32'h8000003f,49'h50000003f);
check(32'h90000000,49'h510000000);
check(32'hffffffc0,49'h57fffffc0);
check(32'hffffffff,49'h57fffffff);
$display("DUAL_MAPPING_PASS: 9 boundaries both AR/AW");$finish;
end endmodule
'''
results={}
for name,code in {'good':sv,'bad_ps_alias':sv.replace("49'h500000000+", "49'h000000000+")}.items():
 f=out/(name+'.sv');f.write_text(code);exe=out/(name+'.vvp')
 c=subprocess.run(['iverilog','-g2012','-s','mapping','-o',str(exe),str(f)],capture_output=True,text=True)
 assert c.returncode==0,c.stderr
 p=subprocess.run(['vvp',str(exe)],capture_output=True,text=True)
 (out/(name+'.log')).write_text(p.stdout+p.stderr)
 ok=p.returncode==0 and 'DUAL_MAPPING_PASS' in p.stdout
 assert ok==(name=='good'),p.stdout
 results[name]='PASS' if ok else 'BUG_DETECTED_AS_EXPECTED'
results['wrapper_sha256']=hashlib.sha256((r/'rtl/gpgpu_subsystem.sv').read_bytes()).hexdigest()
(out/'summary.json').write_text(json.dumps(results,indent=2));print(json.dumps(results,indent=2))

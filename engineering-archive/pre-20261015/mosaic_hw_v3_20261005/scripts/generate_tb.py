from pathlib import Path
import json
r=Path(__file__).resolve().parents[1]
axi=json.loads((r/'config/ports.json').read_text())
s=['`timescale 1ns/1ps','module tb_gpgpu;','  reg clk=0; always #10 clk=~clk;','  reg resetn=0;']
for d,w,n in axi:
    is_reg=d=='output' and n not in ['HP_awready','HP_wready','HP_arready']
    s.append('  '+('reg ' if is_reg else 'wire ')+(w+' ' if w else '')+n+('=0;' if is_reg else ';'))
s += ['  gpgpu_subsystem dut(.*);','  `include "gpu_test_body.svh"','endmodule']
(r/'tb/tb_gpgpu.sv').write_text('\n'.join(s)+'\n')

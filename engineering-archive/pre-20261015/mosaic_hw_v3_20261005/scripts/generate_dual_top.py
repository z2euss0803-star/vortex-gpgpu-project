"""Generate top from actual BD wrapper, retaining the fixed GPU ABI.
Run after make_wrapper. No edits to generated Vivado wrapper.
"""
from pathlib import Path
import re
ROOT = Path(__file__).resolve().parents[1]
wrappers = list((ROOT / 'build' / 'vivado').rglob('ps_bd_wrapper.v'))
if len(wrappers) != 1:
    raise SystemExit(f'Expected one generated wrapper, found {wrappers}')
text = wrappers[0].read_text()
ports = re.findall(r'^\s*(input|output|inout)\s*(\[[^\]]+\])?\s*(\w+)\s*;', text, re.M)
if not ports:
    raise SystemExit('Cannot parse generated BD wrapper ports')
physical = [(d,w,n) for d,w,n in ports if n.startswith('c0_')]
if not physical:
    raise SystemExit('DDR physical ports missing from generated wrapper')
def top_name(n):
    return n.replace('c0_sys_clk_clk_', 'c0_sys_clk_').replace('c0_ddr4_dm_n', 'c0_ddr4_dm_dbi_n')
lines = ['`timescale 1ns/1ps', '// Generated from actual BD ports; DDR pin names match original board XDC.', 'module top (']
lines += [f'  {d} wire {w} {top_name(n)}' + (',' if i<len(physical)-1 else '') for i,(d,w,n) in enumerate(physical)]
lines += [');']
# Use the fixed GPU port ABI, even when SmartConnect drops unused sidebands.
channels = {'araddr':49,'arburst':2,'arcache':4,'arid':6,'arlen':8,'arlock':1,'arprot':3,'arqos':4,'arready':1,'arsize':3,'aruser':1,'arvalid':1,
'awaddr':49,'awburst':2,'awcache':4,'awid':6,'awlen':8,'awlock':1,'awprot':3,'awqos':4,'awready':1,'awsize':3,'awuser':1,'awvalid':1,
'bid':6,'bready':1,'bresp':2,'bvalid':1,'rdata':512,'rid':6,'rlast':1,'rready':1,'rresp':2,'rvalid':1,'wdata':512,'wlast':1,'wready':1,'wstrb':64,'wvalid':1}
for prefix in ('HP','HPM'):
    for s,width in channels.items():
        if prefix=='HPM':
            if s.endswith('addr'): width=40
            elif s in ('arid','awid','bid','rid','aruser','awuser'): width=16
            elif s in ('rdata','wdata'): width=32
            elif s=='wstrb': width=4
        lines.append(f'  wire ' + (f'[{width-1}:0] ' if width>1 else '') + f'{prefix}_{s};')
lines += ['  wire pl_clk0;', '  wire [0:0] axi_resetn;', '  wire calib_complete;',
'  (* ASYNC_REG = "TRUE" *) reg [1:0] calib_sync;',
'  always @(posedge pl_clk0 or negedge axi_resetn[0]) begin',
'    if (!axi_resetn[0]) calib_sync <= 2\'b00;',
'    else calib_sync <= {calib_sync[0], calib_complete};',
'  end', '  wire memory_ready = calib_sync[1];', '  ps_bd_wrapper ps (']
conn=[]
for d,w,n in ports:
    signal = top_name(n) if n.startswith('c0_') else ('pl_clk0' if n=='axi_clk' else n)
    known = n.startswith(('HP_','HPM_','c0_')) or n in ('axi_clk','pl_clk0','axi_resetn','memory_ready','calib_complete')
    if not known: raise SystemExit(f'Unexpected wrapper port {n}; explicitly connect it')
    conn.append(f'    .{n}({signal})')
lines += [',\n'.join(conn), '  );', '  gpgpu_subsystem pl (', '    .clk(pl_clk0), .resetn(axi_resetn[0]), .memory_ready(memory_ready),']
lines += [',\n'.join(f'    .{p}_{s}({p}_{s})' for p in ('HPM','HP') for s in channels), '  );', 'endmodule', '']
(ROOT/'rtl'/'top.sv').write_text('\n'.join(lines))
print(f'Generated top.sv from {wrappers[0]}: {len(physical)} DDR/refclk ports')


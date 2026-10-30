"""Exercise the actual generated reset wrappers with their actual auxiliary tie."""
from pathlib import Path
import os, re, subprocess, sys, json

root = Path(__file__).resolve().parents[1]
bd = root / 'build/vivado/gpgpu_soc.srcs/sources_1/bd/ps_bd'
rtl = (bd/'synth/ps_bd.v').read_text()
ties = []
for inst in ('rst', 'rst_ddr'):
    block = re.search(r'ps_bd_'+inst+r'_0\s+'+inst+r'\s*\((.*?)\);', rtl, re.S)
    assert block, inst
    tie = re.search(r'\.aux_reset_in\(const_(zero|one)_dout\)', block[1])
    assert tie, inst
    ties.append(0 if tie[1] == 'zero' else 1)
assert ties[0] == ties[1], ties
out = root/'evidence/reset_release'
out.mkdir(parents=True, exist_ok=True)
adapter=out/'tb_reset_actual.vhd'
adapter.write_text('entity tb_reset_actual is end entity;\narchitecture test of tb_reset_actual is begin\n'
    + f'  dut: entity work.tb_reset_release generic map(AUX_TIED_LEVEL => {ties[0]});\nend architecture;\n')
env = dict(os.environ, PROCESSOR_ARCHITECTURE='AMD64')
bindir = Path('G:/xilinx/Vivado/2020.1/bin')
def run(name, args):
    p = subprocess.run([str(bindir/(name+'.bat')), *map(str,args)], cwd=out,
                       env=env, capture_output=True, text=True, timeout=180)
    (out/(name+'.log')).write_text(p.stdout+p.stderr)
    assert p.returncode == 0, (name, p.stdout[-1500:], p.stderr[-1500:])
    return p.stdout+p.stderr
run('xvhdl', ['--2008','--work','xil_defaultlib',
    bd/'ip/ps_bd_rst_0/sim/ps_bd_rst_0.vhd',
    bd/'ip/ps_bd_rst_ddr_0/sim/ps_bd_rst_ddr_0.vhd',root/'tb/tb_reset_release.vhd',adapter])
run('xelab', ['-L','proc_sys_reset_v5_0_13','-L','lib_cdc_v1_0_2','-L','unisim',
    'xil_defaultlib.tb_reset_actual','-s','reset_release'])
log=run('xsim', ['reset_release','-runall'])
passed='RESET_RELEASE_PASS' in log and 'RESET_RELEASE_FAIL' not in log
(out/'result.json').write_text(json.dumps({'actual_aux_ties':ties,'pass':passed,
    'scope':'Actual generated vendor reset IP; external resets released, clocks running'},indent=2))
assert passed, log[-2000:]
print('ACTUAL_GENERATED_RESET_RELEASE_PASS')

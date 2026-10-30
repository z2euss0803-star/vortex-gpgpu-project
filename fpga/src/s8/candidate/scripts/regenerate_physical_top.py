from pathlib import Path
import runpy,json
r=Path(__file__).resolve().parents[1]
runpy.run_path(str(r/'scripts/generate_dual_top.py'),run_name='__main__')
p=r/'rtl/top.sv'
s=p.read_text()
w=json.loads((r/'config/build.json').read_text())['AXI_WRITE_WINDOW']
assert s.count('gpgpu_subsystem pl (')==1
p.write_text(s.replace('gpgpu_subsystem pl (',f'gpgpu_subsystem #(.AXI_WRITE_WINDOW({w})) pl ('),encoding='utf-8')

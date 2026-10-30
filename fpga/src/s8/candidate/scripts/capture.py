from pathlib import Path
from zipfile import ZipFile
import hashlib, json, subprocess, io, tarfile
ROOT=Path(__file__).resolve().parents[1]
VAULT=ROOT.parents[1]
repo=VAULT/'50-项目与实验/IC设计与FPGA/Vortex-v2.3可行性验证-2026-09-02/work/vortex'
commit='d425a56ef6299971cce5de629d6fe626b98cafa9'
out=ROOT/'vendor/vortex'
if out.exists(): raise SystemExit('Refusing to overwrite captured Vortex source')
def git(*args): return subprocess.check_output(['git','-c',f'safe.directory={repo.as_posix()}','-C',str(repo),*args])
assert git('rev-parse','HEAD').decode().strip()==commit
archive=git('archive','--format=tar',commit,'hw','kernel','LICENSE','README.md')
out.mkdir(parents=True)
with tarfile.open(fileobj=io.BytesIO(archive)) as tf:
    for m in tf.getmembers():
        dst=(out/m.name).resolve()
        assert dst.is_relative_to(out.resolve()) and not m.issym() and not m.islnk()
        if m.isfile(): dst.parent.mkdir(parents=True,exist_ok=True); dst.write_bytes(tf.extractfile(m).read())
manifest={'commit':commit,'source_repo':str(repo),'snapshot':'git archive, ignores all working-tree edits','inputs':[],
          'vendor_sha256':{str(p.relative_to(out)):hashlib.sha256(p.read_bytes()).hexdigest() for p in out.rglob('*') if p.is_file()}}
ddr=Path(r'F:\share_files\gpgpu_project\17_pl_read_write_ps_ddr\vivado')
captures={
  'board/ddr_source.bd':ddr/'pl_read_write_ps_ddr.srcs/sources_1/bd/design_1/design_1.bd',
  'board/ddr_source.xsa':ddr/'top_ddr_cl17_20260923.xsa',
  'board/hpm_reference.bd':VAULT/'work/ps_pl_ctrl_external_test/board/design_1.bd',
}
for dst,src in captures.items():
    data=src.read_bytes(); p=ROOT/dst; p.parent.mkdir(parents=True,exist_ok=True); p.write_bytes(data)
    manifest['inputs'].append({'source':str(src),'copy':dst,'sha256':hashlib.sha256(data).hexdigest()})
d=json.loads((ROOT/'board/ddr_source.bd').read_text())['design']
ps=d['components']['zynq_ultra_ps_e_0']['parameters']
assert ps['PSU__DDRC__CL']['value']=='17'
assert ps['PSU__DDRC__SPEED_BIN']['value']=='DDR4_2400R'
lines=['# Captured DDR board settings; interface/clock overrides are explicit in create_project.tcl.','set_property -dict [list \\']
for k,v in ps.items(): lines.append('    CONFIG.'+k+' {'+v['value']+'} \\')
lines.append('] $ps')
(ROOT/'board/ps_config.tcl').write_text('\n'.join(lines)+'\n')
(ROOT/'board/source_manifest.json').write_text(json.dumps(manifest,indent=2,ensure_ascii=False),encoding='utf-8')
print('CAPTURE_PASS:',commit,len(manifest['vendor_sha256']),'Vortex files, DDR CL17')

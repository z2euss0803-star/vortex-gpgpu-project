"""Bind compiler products separately; frozen RTL/program/source inputs stay immutable."""
import argparse,datetime,hashlib,json,re
from pathlib import Path
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def unexpected_changes(wd,changes):
 lib=wd.resolve()/'xsim.dir/xil_defaultlib'
 allowed={lib/n for n in ('short.sdb','arch_package.sdb','xil_defaultlib.rlx')}
 return [str(p) for p in changes if p.resolve() not in allowed]
def freeze(wd):
 wd=wd.resolve();target=wd/'compiled-binding.json'
 if target.exists():raise ValueError('Preserve earlier compiled binding')
 inputs=json.loads((wd/'inputs.json').read_text());changes=[Path(p['path']) for p in inputs['inputs'] if sha(Path(p['path']))!=p['sha256']]
 bad=unexpected_changes(wd,changes)
 if bad:raise ValueError('Immutable inputs changed: '+', '.join(bad))
 processes={}
 for name in ('compile','elaborate'):
  process=json.loads((wd/(name+'.process.json')).read_text())
  if process['exit']!=0 or sha(wd/process['log'])!=process['sha256']:raise ValueError('Compiler process binding')
  processes[name]=sha(wd/(name+'.process.json'))
 log=(wd/'compile.log').read_text()
 modules=re.findall(r'analyzing module (\w+)',log)
 if modules!=['short','tb_cnn_h0']:raise ValueError('Unexpected compiler module set')
 original=wd.parent.parent.parent.parent/'vortex_v23_dual_ddr_loopback_20261001/tb/tb_ps_pl_loopback.sv'
 # Package comes from frozen imported DDR source; short is identical to original.
 pattern=r'module short\(in1, in1\);\s*inout in1;\s*endmodule'
 for path in (original,wd/'tb_cnn_h0.sv'):
  if not re.search(pattern,path.read_text()):raise ValueError('Unexpected short stub source')
 if (wd/'tb_cnn_h0.sv').read_text()!=original.read_text().replace('tb_ps_pl_loopback','tb_cnn_h0').replace('ps_host_tests.svh','cnn_adapter.svh'):raise ValueError('Testbench source derivation')
 artifacts={str(p.relative_to(wd)):sha(p) for p in sorted((wd/'xsim.dir/xil_defaultlib').rglob('*')) if p.is_file()}
 for p in sorted((wd/'xsim.dir/cnn_h0').iterdir()):
  if p.is_file() and p.name in ('Compile_Options.txt','dpi.a','xsim.dbg','xsim.mem','xsim.reloc','xsim.rtti','xsim.svtype','xsim.type','xsim.xdbg','xsimk.exe'):artifacts[str(p.relative_to(wd))]=sha(p)
 result=dict(scope='CNN_COMPILED_ARTIFACT_BINDING_NOT_EXECUTION_PASS',created=datetime.datetime.now(datetime.timezone.utc).isoformat(),inputs_sha256=sha(wd/'inputs.json'),source_commit=inputs['source_commit'],processes=processes,
  compiler_owned_changes={str(p):dict(before=next(x['sha256'] for x in inputs['inputs'] if Path(x['path'])==p),after=sha(p)) for p in changes},artifacts=artifacts,board='NOT_RUN')
 target.write_text(json.dumps(result,indent=2)+'\n');print('COMPILED_BINDING_RECORDED_NOT_CNN_PASS',len(artifacts),flush=True)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--workdir',required=True);o=p.parse_args();freeze(Path(o.workdir))

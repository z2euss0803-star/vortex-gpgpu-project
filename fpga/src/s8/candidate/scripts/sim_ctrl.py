"""Positive tests plus rejected bug variants; does not alter production RTL."""
from pathlib import Path
import subprocess,json,hashlib
r=Path(__file__).resolve().parents[1];out=r/'evidence/ctrl';out.mkdir(exist_ok=True)
source=(r/'rtl/gpgpu_ctrl.sv').read_text();summary={}
variants={'good':source,
          'bad_done_without_drain':source.replace('else if(quiet) begin','else if(1\'b1) begin'),
          'bad_start_busy_in_reset':source.replace('(gpu_reset || !gpu_busy)','!gpu_busy')}
for name,text in variants.items():
    f=r/'rtl/gpgpu_ctrl.sv' if name=='good' else out/(name+'.sv')
    if name!='good':f.write_text(text)
    exe=out/(name+'.vvp')
    result=subprocess.run([r'C:\iverilog\bin\iverilog.exe','-g2012','-s','tb_gpgpu_ctrl','-o',str(exe),str(f),str(r/'tb/tb_gpgpu_ctrl.sv')],capture_output=True,text=True)
    (out/(name+'_compile.log')).write_text(result.stdout+result.stderr)
    assert result.returncode==0,result.stderr
    result=subprocess.run([r'C:\iverilog\bin\vvp.exe',str(exe)],cwd=out,capture_output=True,text=True,timeout=30)
    log=result.stdout+result.stderr;(out/(name+'.log')).write_text(log)
    passed='GPGPU_CTRL_PASS:' in log and 'FATAL' not in log
    assert passed==(name=='good'),log
    summary[name]='PASS' if passed else 'BUG_DETECTED_AS_EXPECTED'
summary['rtl_sha256']=hashlib.sha256((r/'rtl/gpgpu_ctrl.sv').read_bytes()).hexdigest()
(out/'summary.json').write_text(json.dumps(summary,indent=2))
print(json.dumps(summary,indent=2))

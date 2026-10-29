from pathlib import Path
import hashlib,json
p=Path(__file__).resolve().parents[1]
t=(p.parent/'mosaic_hw4_soc_v1_20261006/scripts/run_baseline.py').read_text()
t=t.replace("cmd=[str(VENDOR/'bin'/(args[0]+'.bat'))]+args[1:]",
    "cmd=([args[0]] if Path(args[0]).is_absolute() else [str(VENDOR/'bin'/(args[0]+'.bat'))])+args[1:]")
a=t.index("        run('compile_verilog'")
b=t.index("        original=HW/",a)
t=t[:a]+'''        inherited=json.loads((EV/'inherited_compile_library.json').read_text())
        verify_files(inherited['files'])
        ar=VENDOR/'tps/mingw/6.2.0/win64.o/nt/bin/ar.exe'
        run('compile_host',[str(ar),'rcs','xsim.dir/work/xsc/dpi.a','xsim.dir/work/xsc/dpi_backend.win64.obj','xsim.dir/work/xsc/host_logic.win64.obj'])
'''+t[b:]
t=t.replace('hw4_shared_baseline_v1','hw4_shared_baseline_v2')
with (p/'scripts/run_baseline.py').open('x') as f:f.write(t)
names=['run_baseline.py','audit_baseline.py','audit_shared_wave.py','prepare.py','prepare_recovery.py','build_recovery_launcher.py']
d={n:hashlib.sha256((p/'scripts'/n).read_bytes()).hexdigest() for n in names}
with (p/'evidence/launcher_freeze.json').open('x') as f:json.dump(d,f,indent=2)
with (p/'evidence/prelaunch_tool_command_failure.txt').open('x') as f:
    f.write('PowerShell inline Python construction rejected by parser before execution; no launcher created or started by that command. Corrected using saved Python source.\n')
print('RECOVERY_LAUNCHER_FROZEN',len(d))

from pathlib import Path
import hashlib,json,shlex,shutil
from prepare_ops import ROOT,APP,NN,sha
BASE=ROOT.parent/'mosaic_hw4_soc_v4_20261006'
HWBASE=ROOT.parent/'mosaic_hw4_soc_v1_20261006'
HW=ROOT.parent/'mosaic_hw_v4_20261005'
VENDOR=Path('G:/xilinx/Vivado/2020.1')
def main():
    assert not (ROOT/'evidence/input_manifest.json').exists()
    baseline=json.loads((BASE/'evidence/baseline_audit.json').read_text())
    assert baseline['pass_baseline'] is True
    closure=json.loads((BASE/'evidence/completion_binding.json').read_text())
    assert closure['pass_'] is True and closure['live_hw_sources_and_vendor_identities_verified'] is True
    assert closure['audit_sha256']==sha(BASE/'evidence/baseline_audit.json')
    m=json.loads((BASE/'evidence/input_manifest.json').read_text())
    for e in m['files']:assert sha(e['path'])==e['sha256'],e['path']
    wd=ROOT/'build/xsim';wd.mkdir(parents=True,exist_ok=False);(ROOT/'build/tmp').mkdir()
    # The new baseline's private HW4 library, not the old R4 design library.
    shutil.copytree(BASE/'build/xsim/xsim.dir/xil_defaultlib',wd/'xsim.dir/xil_defaultlib')
    inherited=[]
    for p in sorted((wd/'xsim.dir/xil_defaultlib').rglob('*')):
        if p.is_file():
            old=BASE/'build/xsim'/p.relative_to(wd);assert sha(p)==sha(old)
            inherited.append(dict(path=str(p),sha256=sha(p),source=str(old)))
    shutil.copyfile(BASE/'build/xsim/xsim.ini',wd/'xsim.ini')
    for p in (BASE/'build/xsim').glob('*.mem'):shutil.copyfile(p,wd/p.name)
    shutil.copyfile(ROOT/'assets/payload.bin',wd/'payload.bin')
    tb=ROOT/'tb/tb_hw4_mosaic_soc.sv';model=HWBASE/'models/ddr4'
    prj=f'sv xil_defaultlib -i "{model.as_posix()}" -i "{(ROOT/"tb").as_posix()}" -d "DDR4_8G_X16" -d "DDR4_833_Timing" -d "SILENT" -d "FIXED_2400" "{tb.as_posix()}"\n'
    (wd/'ops_tb.prj').write_text(prj)
    src=HW/'build/vivado/gpgpu_soc.sim/sim_1/behav/xsim/elaborate.bat'
    args=shlex.split(next(s[5:] for s in src.read_text().splitlines() if s.startswith('call xelab ')))
    libs=[args[i+1] for i,x in enumerate(args) if x=='-L']
    (ROOT/'evidence/library_names.json').write_text(json.dumps(libs))
    ini=dict(line.split('=',1) for line in (wd/'xsim.ini').read_text().splitlines() if '=' in line)
    external=[]
    for lib in sorted(set(libs+['std','ieee','unisim','unimacro'])):
        if lib=='xil_defaultlib':continue
        d=Path(ini[lib].replace('$RDI_DATADIR',(VENDOR/'data').as_posix()))
        assert d.is_dir(),d
        for p in sorted(d.rglob('*')):
            if p.is_file():external.append(dict(path=str(p),sha256=sha(p),library=lib))
    # Headers are consumed at compile time by generated wrappers and DPI.
    for d in [VENDOR/'data/xilinx_vip/include',VENDOR/'data/ip/xilinx/zynq_ultra_ps_e_vip_v1_0/hdl',VENDOR/'data/xsim/include']:
        for p in sorted(d.rglob('*')):
            if p.is_file():external.append(dict(path=str(p),sha256=sha(p),library='include_source'))
    files=list(m['files'])
    for d in [ROOT/'assets',ROOT/'sw',ROOT/'tb',ROOT/'scripts',ROOT/'tests']:
        for p in sorted(d.rglob('*')):
            if p.is_file():files.append(dict(path=str(p),sha256=sha(p),bytes=p.stat().st_size))
    for p in [wd/'xsim.ini',wd/'ops_tb.prj',wd/'payload.bin',ROOT/'evidence/fixture.json',ROOT/'evidence/library_names.json',BASE/'evidence/completion_binding.json',BASE/'evidence/process_identity_gate.json']:
        files.append(dict(path=str(p),sha256=sha(p),bytes=p.stat().st_size))
    with (ROOT/'evidence/input_manifest.json').open('x') as f:
        json.dump(dict(source_manifest_sha256=m['source_manifest_sha256'],bitstream_sha256=m['bitstream_sha256'],
            xsa_sha256=m['xsa_sha256'],adapter_sha256=m['adapter_sha256'],hw4_source_count=305,
            files=files,inherited_library=inherited,vendor_files=external,
            simulation_checker_override=m['simulation_checker_override'],
            baseline_audit_sha256=sha(BASE/'evidence/baseline_audit.json'),
            baseline_completion_binding_sha256=sha(BASE/'evidence/completion_binding.json'),
            stage_inheritance='Verilog/VHDL compiled once for HW4 in soc_v1; qualified checker and strict baseline in soc_v4; only new operator TB and production device C are compiled here',
            prepared_not_started=True),f,indent=2)
    print('OPERATOR_INPUTS_FROZEN',len(files),'inputs;',len(inherited),'same-candidate compiled files;',len(external),'vendor library/header identities')
if __name__=='__main__':main()

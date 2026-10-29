"""Freeze HW4 full-SoC inputs into a new private simulation workspace."""
from pathlib import Path
import datetime
import hashlib
import json
import re
import shutil

ROOT = Path(__file__).resolve().parents[1]
HW = ROOT.parent/'mosaic_hw_v4_20261005'
OLD = ROOT.parent/'vortex_v23_dual_ddr_functional_r4_20261003'
LOOP = ROOT.parent/'vortex_v23_dual_ddr_loopback_20261001'
VENDOR = Path('G:/xilinx/Vivado/2020.1')
ADAPTER_SHA = 'a0a56aae85978f68fdb84dc6ec9a64de2ab2a296223d577763f365b48e624e58'

def sha(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''): h.update(block)
    return h.hexdigest()

def verify_files(entries):
    for e in entries:
        p=Path(e['path'])
        if not p.is_file() or sha(p)!=e['sha256']: raise ValueError('Input drift: '+str(p))

def new_directory(path):
    Path(path).mkdir(parents=True, exist_ok=False)

def stage_ok(code, text):
    return code==0 and not re.search(r'(?im)^\s*(?:ERROR\s*:|FATAL\b|CRITICAL WARNING)',text)

def absolute_project(text, base):
    # Keep generated order/flags; remove only demo/testbench sources.
    kept=[]
    for line in text.splitlines():
        if '/demo_tb/' in line or '/tb/tb_gpgpu.sv' in line: continue
        def replace(m):
            s=m.group(1)
            if '/' in s or '\\' in s or s.endswith(('.sv','.v','.vhd')):
                p=Path(s)
                if not p.is_absolute(): p=(base/p).resolve()
                return '"'+p.as_posix()+'"'
            return m.group(0)
        kept.append(re.sub(r'"([^"\n]+)"',replace,line))
    return '\n'.join(kept)+'\n'

def prepare():
    prior=json.loads((HW/'evidence/source_manifest.json').read_text())
    assert prior['source_count']==305 and len(prior['entries'])==305
    source_entries=[dict(path=str(HW/e['path']),sha256=e['sha256']) for e in prior['entries']]
    verify_files(source_entries)
    assert sha(HW/'build/compat_rtl/libs/VX_axi_adapter.sv')==ADAPTER_SHA
    new_directory(ROOT/'build')
    (ROOT/'evidence').mkdir(exist_ok=True)
    wd=ROOT/'build/xsim'; wd.mkdir(); (ROOT/'build/tmp').mkdir()
    origin=HW/'build/vivado/gpgpu_soc.sim/sim_1/behav/xsim'
    frozen=ROOT/'source/hw4'
    copied={}
    def copy(p, q):
        p=Path(p);q=Path(q);q.parent.mkdir(parents=True,exist_ok=True)
        h=sha(p)
        if q.exists(): assert sha(q)==h, str(q)
        else: shutil.copyfile(p,q)
        assert sha(p)==h and sha(q)==h
        copied[str(q)]={'path':str(q),'sha256':h,'bytes':q.stat().st_size,'source':str(p)}
        return q
    def freeze_hw(p): return copy(p,frozen/Path(p).relative_to(HW))
    vlog=absolute_project((origin/'tb_gpgpu_vlog.prj').read_text(),origin)
    vhdl=absolute_project((origin/'tb_gpgpu_vhdl.prj').read_text(),origin)
    for quoted in re.findall(r'"([^"\n]+)"',vlog+'\n'+vhdl):
        p=Path(quoted)
        if not p.exists():
            if p.suffix in ('.sv','.v','.vhd'): raise FileNotFoundError(p)
            continue
        if p.is_file() and p.is_relative_to(HW): freeze_hw(p)
        elif p.is_dir() and p.is_relative_to(HW):
            for f in p.rglob('*'):
                if f.is_file() and f.suffix in ('.svh','.vh','.h','.sv','.v'): freeze_hw(f)
    for e in prior['entries']: freeze_hw(HW/e['path'])
    # Full candidate includes are bound to frozen copies, never old compiled libraries.
    vlog=vlog.replace(HW.as_posix(), frozen.as_posix()).replace('"glbl.v"', '"'+(VENDOR/'data/verilog/src/glbl.v').as_posix()+'"')
    vhdl=vhdl.replace(HW.as_posix(), frozen.as_posix())
    ps=copy(LOOP/'models/ps_vip/zynq_ultra_ps_e_vip_v1_0_vl_rfs.sv',ROOT/'models/ps_vip.sv')
    provenance=json.loads((LOOP/'evidence/ps_vip_sparse_provenance.json').read_text())
    assert sha(ps)==provenance['candidate_sha256']
    copy(LOOP/'evidence/ps_vip_sparse_provenance.json',ROOT/'models/ps_vip_provenance.json')
    psflags=f'-i "{VENDOR.as_posix()}/data/ip/xilinx/zynq_ultra_ps_e_vip_v1_0/hdl" -i "{VENDOR.as_posix()}/data/xilinx_vip/include"'
    vlog=f'sv xil_defaultlib {psflags} "{ps.as_posix()}"\n'+vlog.replace('nosort','')
    model=ROOT/'models/ddr4'
    imports=LOOP/'build/ddr_example/ps_bd_ddr_0_ex/imports'
    for p in imports.iterdir():
        if p.is_file() and p.suffix in ('.sv','.v','.vh','.svh'): copy(p,model/p.name)
    for p in (OLD/'tb').iterdir():
        if p.is_file(): copy(p,ROOT/'tb'/p.name)
    for p in (OLD/'sw').iterdir():
        if p.is_file(): copy(p,ROOT/'sw'/p.name)
    copy(OLD/'build/xsim/host_dpi.h',ROOT/'sw/host_dpi.h')
    backend=ROOT/'sw/dpi_backend.c'
    backend.write_text(backend.read_text().replace('"../build/xsim/host_dpi.h"','"host_dpi.h"'))
    copied[str(backend)]['sha256']=sha(backend);copied[str(backend)]['bytes']=backend.stat().st_size
    copied[str(backend)]['adaptation']='Only header path, DPI declarations unchanged'
    flags=f'-i "{model.as_posix()}" -i "{(ROOT/"tb").as_posix()}" -d "DDR4_8G_X16" -d "DDR4_833_Timing" -d "SILENT" -d "FIXED_2400"'
    for name in ['arch_package.sv','proj_package.sv','ddr4_model.sv','interface.sv']:
        vlog+=f'sv xil_defaultlib {flags} "{(model/name).as_posix()}"\n'
    vlog+=f'sv xil_defaultlib {flags} "{(ROOT/"tb/tb_ps_host_c.sv").as_posix()}"\nnosort\n'
    (wd/'hw4_vlog.prj').write_text(vlog); (wd/'hw4_vhdl.prj').write_text(vhdl)
    copy(origin/'xsim.ini',wd/'xsim.ini')
    memories={}
    for p in (HW/'build/vivado').rglob('*.mem'):
        h=sha(p)
        if p.name in memories: assert h==memories[p.name],p
        else: copy(p,wd/p.name);memories[p.name]=h
    assert not (wd/'xsim.dir').exists(), 'Private compile library must start empty'
    for p in [ROOT/'scripts/prepare.py',ROOT/'tests/test_prepare.py',wd/'hw4_vlog.prj',wd/'hw4_vhdl.prj']:
        copied[str(p)]={'path':str(p),'sha256':sha(p),'bytes':p.stat().st_size,'source':str(p)}
    bit=HW/'build/artifacts/top.bit'; xsa=HW/'build/artifacts/gpgpu_with_bit.xsa'
    result={'prepared_at':datetime.datetime.now().astimezone().isoformat(),
            'source_manifest_sha256':sha(HW/'evidence/source_manifest.json'),
            'bitstream_sha256':sha(bit),'xsa_sha256':sha(xsa),'adapter_sha256':ADAPTER_SHA,
            'hw4_source_count':305,'private_compile_library_reused':False,
            'scope':'Actual HW4 top/BD/IP + PS VIP + DDR device; shared C cache ownership model, no A53 ISA/FSBL/MMU/board',
            'files':list(copied.values()),'memory_files':memories}
    verify_files(result['files'])
    (ROOT/'evidence/input_manifest.json').write_text(json.dumps(result,indent=2))
    print('HW4_SOC_PREPARED',len(result['files']),'frozen inputs; 305 hardware sources; new private compile library')

if __name__=='__main__': prepare()

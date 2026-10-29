"""Capture original provenance and current source hashes without modifying sources."""
from pathlib import Path
import hashlib, json, datetime
import xml.etree.ElementTree as ET

root=Path(__file__).resolve().parents[1]
baseline=root.parent/'vortex_ppa_ab_20260927/v23_4tex'
example=Path(r'F:\share_files\gpgpu_project\09_pl_ddr4_test')
def digest(p):
    return {'path':str(p),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
original=[]
for folder in ('rtl','config','build/compat_rtl','board'):
    original.extend(digest(p) for p in sorted((baseline/folder).rglob('*')) if p.is_file())
for name in ('pl_ddr4.xpr','pl_ddr4.srcs/sources_1/ip/ddr4_1/ddr4.xci','pl_ddr4.srcs/sources_1/new/top.v','pl_ddr4.srcs/sources_1/new/mem_burst.v','pl_ddr4.srcs/sources_1/new/mem_test.v','pl_ddr4.srcs/constrs_1/new/ddr4_set.xdc','pl_ddr4.srcs/constrs_1/new/system.xdc','pl_ddr4.runs/impl_1/top_timing_summary_routed.rpt','pl_ddr4.runs/impl_1/top_utilization_placed.rpt'):
    original.append(digest(example/name))
current=[]
for folder in ('rtl','config','build/compat_rtl','board','scripts','tb','sw'):
    current.extend(digest(p) for p in sorted((root/folder).rglob('*')) if p.is_file() and p.suffix in ('.sv','.v','.vh','.svh','.tcl','.xdc','.json','.xci','.py','.ps1','.c','.h'))
ns='{http://www.spiritconsortium.org/XMLSchema/SPIRIT/1685-2009}'
xci=ET.parse(root/'board/ddr4_original.xci')
params={e.attrib.get(ns+'referenceId'):e.text for e in xci.iter(ns+'configurableElementValue') if e.attrib.get(ns+'referenceId','').startswith('PARAM_VALUE.')}
payload={'captured':datetime.datetime.now().astimezone().isoformat(),'baseline':str(baseline),'ddr_example':str(example),'original_inputs':original,'candidate_inputs':current,'ddr_example_params':params}
out=root/'evidence'/('inputs_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S')+'.json')
out.write_text(json.dumps(payload,indent=2,ensure_ascii=False),encoding='utf-8')
print(out)
print('Original files',len(original),'candidate files',len(current))

"""Collect actual routed evidence and verify matching offline deliverables.

Does not run simulation, synthesis, implementation, or access a board.
"""
from pathlib import Path
import datetime
import hashlib
import json
import re
import zipfile
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
ev = root / 'evidence'
baseline = root.parent / 'vortex_ppa_ab_20260927/v23_4tex/evidence'

def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()

def resources(path):
    text = path.read_text()
    result = {}
    for key, label in {'LUT': 'CLB LUTs', 'registers': 'CLB Registers',
                       'BRAM_tile': 'Block RAM Tile', 'DSP': 'DSPs', 'CLB': 'CLB'}.items():
        m = re.search(r'^\|\s*' + re.escape(label) + r'\s*\|\s*([\d.]+)\s*\|\s*\d+\s*\|\s*(\d+)\s*\|\s*([\d.]+)', text, re.M)
        assert m, (path, label)
        result[key] = dict(used=float(m[1]), available=int(m[2]), percent=float(m[3]))
    return result

timing = (ev / 'timing_summary.rpt').read_text()
row = re.search(r'WNS\(ns\).*?\n\s*-.*?\n\s*([\d.\-]+(?:\s+[\d.\-]+){11})', timing, re.S)
assert row, 'Timing summary row missing'
values = list(map(float, row[1].split()))
assert values[0] >= 0 and values[4] >= 0 and values[8] >= 0
assert values[2] == values[6] == values[10] == 0
timing_result = dict(zip(['WNS', 'TNS', 'setup_failed', 'setup_total', 'WHS', 'THS',
                         'hold_failed', 'hold_total', 'WPWS', 'TPWS', 'pulse_failed', 'pulse_total'], values))
for check in ('no_clock', 'unconstrained_internal_endpoints', 'no_input_delay'):
    assert f'checking {check} (0)' in timing
assert 'checking no_output_delay (1)' in timing and 'c0_ddr4_reset_n' in timing
route = (ev / 'route_status.rpt').read_text()
assert re.search(r'# of nets with routing errors\.+\s*:\s*0\s*:', route)
assert 'BLACKBOX_COUNT=0' in (ev / 'routed_structure_audit.txt').read_text()
skew = (ev / 'bus_skew.rpt').read_text()
skews = list(map(float, re.findall(r'Slack \(MET\)\s*:\s*([\d.]+)ns', skew)))
assert skews and 'VIOLATED' not in skew
cdc = (ev / 'cdc.rpt').read_text()
cdc_warnings = re.findall(r'^\s*\d+\s+CDC-15\s+Warning.*$', cdc, re.M)
assert len(cdc_warnings) == 1213
assert all('memory_fabric' in x and 'xpm_memory_base_inst' in x for x in cdc_warnings)
assert not re.search(r'^CDC-\d+\s+Critical', cdc, re.M)
hier = (ev / 'utilization_routed_hier.rpt').read_text()
features = {}
for feature in ('fpu_unit', 'tcu_unit', 'tex_unit'):
    features[feature] = len(re.findall(r'^\|\s*' + feature + r'\s*\|', hier, re.M))
    assert features[feature] == 4, features
for core in range(4):
    assert re.search(r'^\|\s*g_cores\[' + str(core) + r'\]\.core\s*\|', hier, re.M)
for block in ('ddr', 'cdma', 'memory_fabric'):
    assert re.search(r'^\|\s*' + block + r'\s*\|', hier, re.M)
sim_source = root.parent/'vortex_v23_dual_ddr_20260929'
# Only the BD auxiliary-reset wiring changes. Reuse the prior GPU-only test
# after checking all its design/test inputs; record its original location.
gpu_reuse_files = []
for folder in ('rtl', 'config', 'kernel', 'vendor', 'build/compat_rtl'):
    for path in sorted((root/folder).rglob('*')):
        if path.is_file():
            rel = path.relative_to(root)
            assert sha(path) == sha(sim_source/rel), 'GPU simulation input changed: '+str(rel)
            gpu_reuse_files.append(str(rel))
for rel in ('tb/tb_gpgpu.sv', 'scripts/sim_gpu.py'):
    assert sha(root/rel) == sha(sim_source/rel), rel
sim = (sim_source/'evidence/gpu_rtl/run.log').read_text()
reset_result=json.loads((ev/'reset_release/result.json').read_text())
assert reset_result['pass'] and reset_result['actual_aux_ties'] == [1,1]
assert json.loads((ev/'generated_audit.json').read_text())['all_pass']
for marker in ('GPGPU_REAL_RTL_PASS', 'NATIVE_TCU_TEX_RTL_PASS', 'DUAL_DDR_COVERAGE_PASS'):
    assert marker in sim, marker
elf_headers = (root / 'build/dual_sw/elf_headers.txt').read_text()
assert 'AArch64' in elf_headers
software_log = (ev / 'software_build.log').read_bytes()
software_log = software_log.decode('utf-16' if software_log.startswith(b'\xff\xfe') else 'utf-8-sig', errors='replace')
assert 'DUAL_A53_ELF_BUILD_PASS' in software_log
artifacts = [root / p for p in ('build/artifacts/top.bit', 'build/artifacts/gpgpu_with_bit.xsa',
    'build/artifacts/top_routed.dcp', 'build/dual_sw/dual_ddr_host.elf',
    'build/dual_sw/dual_ddr_host.map', 'build/dual_sw/software_manifest.sha256')]
artifact_hashes = {p.relative_to(root).as_posix(): dict(bytes=p.stat().st_size, sha256=sha(p)) for p in artifacts}
with zipfile.ZipFile(root / 'build/artifacts/gpgpu_with_bit.xsa') as z:
    bits = [n for n in z.namelist() if n.endswith('.bit')]
    assert len(bits) == 1, bits
    assert hashlib.sha256(z.read(bits[0])).hexdigest() == artifact_hashes['build/artifacts/top.bit']['sha256']
    assert ET.fromstring(z.read('sysdef.xml')).find("File[@Type='BIT']").get('Name') == bits[0]
    assert {'name': bits[0], 'type': 'FULL_BIT'} in json.loads(z.read('xsa.json'))['files']
ps_xci = root / 'build/vivado/gpgpu_soc.srcs/sources_1/bd/ps_bd/ip/ps_bd_ps_0/ps_bd_ps_0.xci'
ns = '{http://www.spiritconsortium.org/XMLSchema/SPIRIT/1685-2009}'
params = {e.get(ns + 'referenceId'): e.text for e in ET.parse(ps_xci).iter(ns + 'configurableElementValue')}
clocks = {'requested_gpu_mhz': float(params['PARAM_VALUE.PSU__CRL_APB__PL0_REF_CTRL__FREQMHZ']),
          'ps_ip_actual_gpu_mhz': float(params['PARAM_VALUE.PSU__CRL_APB__PL0_REF_CTRL__ACT_FREQMHZ'])}
for name, key in (('clk_pl_0', 'sta_gpu'), ('mmcm_clkout0', 'sta_ddr_ui')):
    match = re.search(r'^\s*' + name + r'\s+\{[^}]+\}\s+([\d.]+)\s+([\d.]+)', timing, re.M)
    assert match
    clocks[key + '_period_ns'] = float(match[1])
    clocks[key + '_display_mhz'] = float(match[2])
manifest = (root / 'build/dual_sw/software_manifest.sha256').read_text()
manifest_rows = [line.split('  ', 2) for line in manifest.splitlines() if line.strip()]
assert len(manifest_rows) == 9
for digest, label, path in manifest_rows:
    assert sha(Path(path)) == digest.lower(), label
input_snapshot = sorted(ev.glob('inputs_*.json'))[-1]
inputs = json.loads(input_snapshot.read_text(encoding='utf-8'))
for entry in inputs['original_inputs']:
    assert sha(Path(entry['path'])) == entry['sha256'], 'Original source changed: ' + entry['path']
for entry in inputs['candidate_inputs']:
    path = Path(entry['path'])
    relative = path.relative_to(root).as_posix()
    if relative.startswith(('rtl/', 'config/', 'board/', 'build/compat_rtl/')):
        assert sha(path) == entry['sha256'], 'Hardware input changed after build: ' + relative
current, old = resources(ev / 'utilization_routed.rpt'), resources(baseline / 'utilization_routed.rpt')
result = dict(captured=datetime.datetime.now().astimezone().isoformat(), tool='Vivado/Vitis 2020.1',
    device='xczu11eg-ffvc1156-2-i', resources=current, baseline_resources=old,
    deltas={key: current[key]['used'] - old[key]['used'] for key in current}, timing=timing_result, clocks=clocks,
    bus_skew_checks=len(skews), bus_skew_min_slack_ns=min(skews), features=features,
    cdc_15_warnings=len(cdc_warnings), cdc_15_scope='SmartConnect XPM FIFO memory only',
    external_unconstrained_reset_port='c0_ddr4_reset_n', board_tested=False, artifacts=artifact_hashes,
    verified_input_snapshot=input_snapshot.name, original_sources_unchanged=True,
    gpu_simulation_reused_from=str(sim_source/'evidence/gpu_rtl/run.log'),
    gpu_simulation_unchanged_inputs=len(gpu_reuse_files)+2, reset_release=reset_result)
(ev / 'delivery_audit.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
print('DELIVERY_AUDIT_PASS: offline evidence and matching artifacts verified; board not tested')
print(json.dumps({k: result[k] for k in ('resources', 'deltas', 'timing', 'bus_skew_checks', 'bus_skew_min_slack_ns')}, indent=2))

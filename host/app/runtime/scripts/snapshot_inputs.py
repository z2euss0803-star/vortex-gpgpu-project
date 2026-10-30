"""Freeze only required source inputs; never copy a Git directory or credentials."""
import hashlib
import json
import pathlib
import shutil
import subprocess

S = pathlib.Path(__file__).resolve().parents[1]
VAULT = S.parents[1]
V = VAULT / '50-项目与实验/IC设计与FPGA/Vortex-v2.3可行性验证-2026-09-02/work/vortex'
T = VAULT / 'work/ab_petalinux_verification_20261007/20261007-151536'
out = S / 'vendor/vortex'
records = []
for directory in ['runtime/include', 'runtime/common', 'runtime/stub']:
    for p in (V / directory).rglob('*'):
        if p.is_file():
            records.append((p, out / p.relative_to(V)))
for name in ['hw/rtl/VX_config.vh', 'hw/rtl/VX_types.vh', 'hw/scripts/gen_config.py', 'sim/common/mem_alloc.h', 'LICENSE']:
    p = V / name
    if p.is_file():
        records.append((p, out / name))
for name in ['gpgpu_hw.h', 'host_platform.h', 'host_logic.c', 'board_backend.c', 'vecadd16.h']:
    records.append((VAULT / 'work/vortex_v23_dual_ddr_functional_r4_20261003/delivery/source/sw' / name, S / 'vendor/h0' / name))
for name in ['gpgpu_with_bit.xsa', 'input_manifest_r4.json']:
    records.append((T / 'inputs/r4_diag' / name, S / 'vendor/h0' / name))
records.append((T / 'engineering_verify_20261008/built-system.dts', S / 'vendor/h0/built-system.dts'))
records.append((VAULT / 'work/vortex_v23_dual_ddr_functional_r4_20261003/delivery/source_manifest.json', S / 'vendor/h0/source_manifest.json'))
for name in ['gpgpu_ctrl.sv', 'gpgpu_subsystem.sv']:
    records.append((VAULT / 'work/vortex_v23_dual_ddr_resetfix_20261001/rtl' / name, S / 'vendor/h0' / name))
manifest = []
for original, dest in records:
    data = original.read_bytes()
    sha = hashlib.sha256(data).hexdigest()
    if dest.exists():
        assert dest.read_bytes() == data, 'Refusing to overwrite a changed snapshot: ' + str(dest)
    else:
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(str(original), str(dest))
    assert hashlib.sha256(original.read_bytes()).hexdigest() == sha
    manifest.append(dict(source=str(original), copy=str(dest.relative_to(S)).replace('\\', '/'), bytes=len(data), sha256=sha))
result = dict(vortex_commit=subprocess.check_output(['git', '-C', str(V), 'rev-parse', 'HEAD'], text=True).strip(), sources=manifest)
(S / 'evidence/20261009-takeover/inputs.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
print('SNAPSHOT_INPUTS', len(manifest))

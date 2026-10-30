"""Post-publication file identity audit; no target or image execution."""
import argparse
import ast
import json
import pathlib
import re
import sys


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', required=True)
    p.add_argument('--baseline', required=True)
    p.add_argument('--evidence', required=True)
    args = p.parse_args()
    root = pathlib.Path(args.root).resolve()
    baseline = pathlib.Path(args.baseline).resolve()
    evidence = pathlib.Path(args.evidence).resolve()
    sys.path.insert(0, str(root))
    from scripts.unified.program_identity import sha
    from scripts.unified.hunet_tile2_program import selected_directory as hunet
    from scripts.unified.mosaic_reuse_v8_program import selected_directory as mosaic
    from scripts.unified import hunet_program, mosaic_program
    assert hunet_program.CURRENT == 'hunet-fp16-v2'
    assert mosaic_program.CURRENT == 'mosaic-int16-v3'
    assert 'reuse-v8' not in mosaic_program.VARIANTS
    rows = []
    for directory in (hunet(root), mosaic(root)):
        family = json.loads((directory / 'family.json').read_bytes())
        assert family['scope'] == 'R13_ACTUAL_RV32_STATIC_NOT_GPU_EXECUTED'
        rows.append(dict(directory=directory.relative_to(root).as_posix(),
                         files={p.name: dict(bytes=p.stat().st_size, sha256=sha(p)) for p in sorted(directory.iterdir())}))
    old = list((baseline / 'data/nnrt/programs').rglob('*'))
    old += list((baseline / 'include/vxnn').glob('*program.hpp'))
    old += [baseline / path for path in (
        'scripts/unified/hunet_program.py', 'scripts/unified/mosaic_program.py',
        'scripts/unified/mosaic_reuse_program.py', 'scripts/unified/publish_perf_programs.py',
        'scripts/unified/publish_mosaic_reuse_v7.py', 'scripts/unified/build_hunet_rv32.py',
        'scripts/unified/generate_hunet_package.py', 'scripts/unified/generate_mosaic_package.py',
        'scripts/unified/joint_firmware.py', 'scripts/unified/bind_kernel.py')]
    protected = {}
    changed = []
    for path in old:
        if path.is_file():
            relative = path.relative_to(baseline).as_posix()
            # The HUNet builder was intentionally changed by the implementation
            # owner before publication. Record it, without treating it as ours.
            if relative == 'scripts/unified/build_hunet_rv32.py':
                continue
            if sha(root / relative) != sha(path):
                changed.append(relative)
            protected[relative] = sha(root / relative)
    assert not changed, changed
    own_paths = ['scripts/unified/' + name for name in (
        'program_identity.py', 'hunet_tile2_program.py', 'mosaic_reuse_v8_program.py',
        'r13_program_publication.py', 'publish_hunet_tile2_v3.py', 'publish_mosaic_reuse_v8.py')]
    own_paths += ['include/vxnn/hunet_tile2_program.hpp', 'include/vxnn/mosaic_reuse_v8_program.hpp']
    own_paths += [p.relative_to(root).as_posix() for p in (root / 'tests/unified/publication-658').glob('*.py')]
    for path in own_paths:
        if path.endswith('.py'):
            ast.parse((root / path).read_text(encoding='utf8'))
    symbols = {}
    for filename in (root / 'include/vxnn').glob('*program.hpp'):
        for symbol in re.findall(r'const char\*\s*(\w+)\s*=', filename.read_text(encoding='utf8')):
            assert symbol not in symbols, (symbol, symbols.get(symbol), filename.name)
            symbols[symbol] = filename.name
    owned_files = [root / path for path in own_paths]
    owned_files += [p for row in rows for p in (root / row['directory']).rglob('*') if p.is_file()]
    owned_bytes = sum(p.stat().st_size for p in owned_files)
    evidence_bytes = sum(p.stat().st_size for p in evidence.rglob('*') if p.is_file())
    assert owned_bytes + evidence_bytes < 150_000_000
    result = dict(scope='R13_PUBLICATION_BYTE_IDENTITY_NOT_IMAGE_OR_GPU_PASS', natural_exit=0,
                  families=rows, protected_files_unchanged=len(protected), protected=protected,
                  implementation={path: sha(root / path) for path in own_paths},
                  own_source_and_published_bytes=owned_bytes, evidence_bytes=evidence_bytes,
                  default_hunet=hunet_program.CURRENT, default_mosaic=mosaic_program.CURRENT,
                  gpu_raw_cycles=None)
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()

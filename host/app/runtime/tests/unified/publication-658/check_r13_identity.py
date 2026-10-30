"""Exercise new actual RV32 admissions and historical drift protection."""
import argparse
import hashlib
import json
import pathlib
import shutil
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True)
    parser.add_argument('--baseline', required=True)
    parser.add_argument('--hunet-build', required=True)
    parser.add_argument('--mosaic-build', required=True)
    parser.add_argument('--workspace', required=True)
    args = parser.parse_args()
    root = pathlib.Path(args.root).resolve()
    sys.path.insert(0, str(root))
    from scripts.unified.program_identity import selected, compiled_sources, sha
    from scripts.unified.mosaic_reuse_program import selected_directory as old_mosaic
    # Bind the historical family explicitly so a later integration can change
    # the global HUNet default without weakening this historical regression.
    def old_hunet(path):
        return selected(path, 'hunet-fp16-v2', 'span32-v2',
                        '79319f1611265bc2ad131b91642de7ddbe0c30d4b3621ce3a8060e90e1114c89',
                        'include/vxnn/hunet_span_program.hpp',
                        lambda f, s: dict(hunet_span_code_sha256=f['code_sha256'],
                                          hunet_span_dispatch_sha256=s['device/vxnn/hunet_scalar_dispatch.h']))
    workspace = pathlib.Path(args.workspace)
    workspace.mkdir(parents=True, exist_ok=True)
    cases = [
        ('hunet-fp16-v3', 'span32-tile2-v3', pathlib.Path(args.hunet_build),
         'hunet-fp16-v2', 'device/vxnn/hunet_entry.cpp',
         'include/vxnn/hunet_tile2_program.hpp',
         '8cb3e18144d60a05e57009d23d02f0da8d07d9741b1bb780d4a0d4fa144990a6'),
        ('mosaic-int16-v8', 'reuse-v8', pathlib.Path(args.mosaic_build),
         'mosaic-int16-v7', 'device/vxnn/mosaic_reuse_entry.cpp',
         'include/vxnn/mosaic_reuse_v8_program.hpp',
         '618632402b615385dd165f7fa25a4526843cb7e975517a3c7fb16d27435fa34c'),
    ]
    failures = []
    admitted = 0
    rejected = 0
    for name, variant, build, old_name, entry, header, expected_code in cases:
        with tempfile.TemporaryDirectory(dir=workspace, prefix=name + '-') as tmp:
            private = pathlib.Path(tmp)
            olddir = root / 'data/nnrt/programs' / old_name
            old = json.loads((olddir / 'family.json').read_bytes())
            rec = json.loads((build / 'receipt.json').read_bytes())
            assert rec['flat']['sha256'] == expected_code
            sources = compiled_sources(rec, entry)
            if name.startswith('hunet'):
                for path in ('scripts/unified/hunet_schema.py', 'scripts/unified/hunet_rodata.py'):
                    sources[path] = sha(root / path)
            family = dict(old)
            family.update(variant=variant, code_sha256=expected_code,
                          program_receipt_sha256=sha(build / 'receipt.json'),
                          sources=[dict(path=p, sha256=d) for p, d in sorted(sources.items())],
                          legacy=dict(directory='data/nnrt/programs/' + old_name,
                                      family_sha256=sha(olddir / 'family.json'), code_sha256=old['code_sha256']))
            if name.startswith('mosaic'):
                family['build_variant'] = 'reuse-v7'
            pins = {'test_code_sha256': expected_code}
            for path in sources:
                target = private / path
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(root / path, target)
            target_header = private / header
            target_header.parent.mkdir(parents=True, exist_ok=True)
            target_header.write_text('inline constexpr const char*test_code_sha256="' + expected_code + '";\n')
            directory = private / 'data/nnrt/programs' / name
            directory.mkdir(parents=True)
            private_old = private / 'data/nnrt/programs' / old_name
            private_old.mkdir()
            for filename in ('family.json', 'code.bin'):
                shutil.copyfile(olddir / filename, private_old / filename)
            for path, dest in ((build / 'code.bin', 'code.bin'), (build / 'kernel.elf', 'kernel.elf'),
                               (build / 'receipt.json', 'rv32-build.json'), (olddir / 'args-schema.bin', 'args-schema.bin')):
                shutil.copyfile(path, directory / dest)
            if name.startswith('mosaic'):
                shutil.copyfile(build / 'rodata.bin', directory / 'rodata.bin')
            (directory / 'family.json').write_text(json.dumps(family))
            try:
                result = selected(private, name, variant, family['schema_sha256'], header, lambda f, s: pins)
                assert result == directory
                admitted += 1
            except ValueError as exc:
                failures.append(dict(name=name, error=str(exc)))
                continue
            # Arbitrary variants must never reuse the v3/v8 exception.
            bad = dict(family, variant='unrecognized-future-variant')
            (directory / 'family.json').write_text(json.dumps(bad))
            try:
                selected(private, name, bad['variant'], family['schema_sha256'], header, lambda f, s: pins)
            except ValueError:
                rejected += 1
            else:
                failures.append(dict(name=name, error='UNRECOGNIZED_VARIANT_ACCEPTED'))
    for selector in (old_hunet, old_mosaic):
        selector(pathlib.Path(args.baseline))
        try:
            selector(root)
        except ValueError as exc:
            assert 'PUBLISHED_GPU_SOURCE_DRIFT' in str(exc), str(exc)
            rejected += 1
        else:
            failures.append(dict(error='LEGACY_SOURCE_DRIFT_ACCEPTED'))
    result = dict(scope='R13_ACTUAL_STATIC_IDENTITY_NOT_GPU_PASS', admitted=admitted,
                  rejected= rejected, failures=failures, gpu_raw_cycles=None)
    print(json.dumps(result, indent=2))
    return 1 if failures else 0


if __name__ == '__main__':
    raise SystemExit(main())

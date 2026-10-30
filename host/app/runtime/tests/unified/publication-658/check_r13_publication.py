"""Publish real builds in a private overlay, then reject identity corruption."""
import argparse
import importlib
import json
import pathlib
import shutil
import sys
import tempfile


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', required=True)
    p.add_argument('--hunet-build', required=True)
    p.add_argument('--mosaic-build', required=True)
    p.add_argument('--workspace', required=True)
    args = p.parse_args()
    root = pathlib.Path(args.root).resolve()
    workspace = pathlib.Path(args.workspace).resolve()
    workspace.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(root))
    from scripts.unified.program_identity import compiled_sources, sha
    try:
        hpub = importlib.import_module('scripts.unified.publish_hunet_tile2_v3')
        mpub = importlib.import_module('scripts.unified.publish_mosaic_reuse_v8')
        hsel = importlib.import_module('scripts.unified.hunet_tile2_program')
        msel = importlib.import_module('scripts.unified.mosaic_reuse_v8_program')
    except ModuleNotFoundError as exc:
        print(json.dumps(dict(failure='R13_INDEPENDENT_PUBLICATION_API_REQUIRED', missing=exc.name)))
        return 1
    families = [
        dict(name='hunet-fp16-v3', old='hunet-fp16-v2', build=pathlib.Path(args.hunet_build),
             entry='device/vxnn/hunet_entry.cpp', helper='device/vxnn/hunet_grouped_gemm.h',
             pin='include/vxnn/hunet_tile2_program.hpp', publish=hpub.publish, select=hsel.selected_directory,
             code='8cb3e18144d60a05e57009d23d02f0da8d07d9741b1bb780d4a0d4fa144990a6'),
        dict(name='mosaic-int16-v8', old='mosaic-int16-v7', build=pathlib.Path(args.mosaic_build),
             entry='device/vxnn/mosaic_reuse_entry.cpp', helper='device/vxnn/mosaic_work_layout_v1.h',
             pin='include/vxnn/mosaic_reuse_v8_program.hpp', publish=mpub.publish, select=msel.selected_directory,
             code='618632402b615385dd165f7fa25a4526843cb7e975517a3c7fb16d27435fa34c'),
    ]
    rejected = 0
    def rejects(action, label):
        nonlocal rejected
        try:
            action()
        except ValueError:
            rejected += 1
        else:
            raise AssertionError('CORRUPT_IDENTITY_ACCEPTED ' + label)

    with tempfile.TemporaryDirectory(dir=workspace, prefix='publication-actual-') as tmp:
        private = pathlib.Path(tmp).resolve()
        assert private.is_relative_to(workspace)
        source_paths = set()
        for item in families:
            receipt = json.loads((item['build'] / 'receipt.json').read_bytes())
            source_paths.update(compiled_sources(receipt, item['entry']))
            shutil.copytree(root / 'data/nnrt/programs' / item['old'], private / 'data/nnrt/programs' / item['old'])
        source_paths.update(('scripts/unified/hunet_schema.py', 'scripts/unified/hunet_rodata.py',
                             'scripts/unified/mosaic_schema_reuse_v4.py'))
        for path in (root / 'scripts/unified').glob('*publication*.py'):
            source_paths.add(path.relative_to(root).as_posix())
        for filename in ('publish_hunet_tile2_v3.py', 'publish_mosaic_reuse_v8.py', 'program_identity.py',
                         'hunet_tile2_program.py', 'mosaic_reuse_v8_program.py'):
            source_paths.add('scripts/unified/' + filename)
        for path in source_paths:
            target = private / path
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(root / path, target)
        for header in (root / 'include/vxnn').glob('*program.hpp'):
            if header.name not in ('hunet_tile2_program.hpp', 'mosaic_reuse_v8_program.hpp'):
                target = private / 'include/vxnn' / header.name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(header, target)
        legacy = {p: sha(p) for base in (private / 'data/nnrt/programs', private / 'include/vxnn')
                  for p in base.rglob('*') if p.is_file()}

        for item in families:
            out = private / 'data/nnrt/programs' / item['name']
            header = private / item['pin']
            with tempfile.TemporaryDirectory(dir=workspace, prefix='build-negative-') as btmp:
                candidate = pathlib.Path(btmp)
                for filename in ('receipt.json', 'code.bin', 'kernel.elf') + (('rodata.bin',) if item['name'].startswith('mosaic') else ()):
                    shutil.copyfile(item['build'] / filename, candidate / filename)
                original = (candidate / 'receipt.json').read_bytes()
                mutations = [
                    lambda r: r.update(natural_exit=True),
                    lambda r: r.update(target_executed=True),
                    lambda r: r.update(gpu_raw_cycles=0),
                    lambda r: r.update(object_ownership={}),
                    lambda r: r.update(stack=dict(bytes=8128)),
                    lambda r: r['commands'][0].update(natural_exit=1),
                    lambda r: r['sources'].pop(item['entry']),
                ]
                if item['name'].startswith('hunet'):
                    mutations += [lambda r: r.update(dispatch_variant='span32-v2')]
                else:
                    mutations += [lambda r: r.update(variant='reuse-v8'), lambda r: r.update(private_abi=3)]
                for change in mutations:
                    changed = json.loads(original)
                    change(changed)
                    (candidate / 'receipt.json').write_text(json.dumps(changed))
                    rejects(lambda: item['publish'](private, candidate), item['name'] + '/bad-build')
                    assert not out.exists() and not header.exists(), 'REJECTED_BUILD_PUBLISHED_FILES'
                (candidate / 'receipt.json').write_bytes(original)
                old_code = (private / 'data/nnrt/programs' / item['old'] / 'code.bin').read_bytes()
                (candidate / 'code.bin').write_bytes(old_code)
                changed = json.loads(original)
                changed['flat']['sha256'] = sha(candidate / 'code.bin')
                (candidate / 'receipt.json').write_text(json.dumps(changed))
                rejects(lambda: item['publish'](private, candidate), item['name'] + '/legacy-code')
                assert not out.exists() and not header.exists()
            source = private / item['helper']
            original_source = source.read_bytes()
            source.write_bytes(original_source + b'\n// drift\n')
            rejects(lambda: item['publish'](private, item['build']), item['name'] + '/source-drift')
            source.write_bytes(original_source)
            assert not out.exists() and not header.exists()
            result = item['publish'](private, item['build'])
            assert result['natural_exit'] == 0 and result['gpu_raw_cycles'] is None
            assert item['select'](private) == out
            assert sha(out / 'code.bin') == item['code']
            rejects(lambda: item['publish'](private, item['build']), item['name'] + '/republish')
            assert all(sha(path) == digest for path, digest in legacy.items())
            family_bytes = (out / 'family.json').read_bytes()
            family = json.loads(family_bytes)
            assert family['legacy']['code_sha256'] != item['code']
            assert (out / 'rv32-build.json').read_bytes() == (item['build'] / 'receipt.json').read_bytes()
            for filename in ('code.bin', 'kernel.elf', 'args-schema.bin', 'rv32-build.json') + (('rodata.bin',) if item['name'].startswith('mosaic') else ()):
                path = out / filename
                original = path.read_bytes()
                path.write_bytes(bytes([original[0] ^ 1]) + original[1:])
                rejects(lambda: item['select'](private), item['name'] + '/' + filename)
                path.write_bytes(original)
            original_header = header.read_bytes()
            header.write_bytes(original_header.replace(item['code'].encode(), b'0' * 64))
            rejects(lambda: item['select'](private), item['name'] + '/pin')
            header.write_bytes(original_header)
            for row in family['sources']:
                path = private / row['path']
                original = path.read_bytes()
                path.write_bytes(original + b'\n// compiled dependency drift\n')
                rejects(lambda: item['select'](private), item['name'] + '/dependency/' + row['path'])
                path.write_bytes(original)
            changed = json.loads(family_bytes)
            changed['sources'] = [r for r in changed['sources'] if r['path'] != item['helper']]
            (out / 'family.json').write_text(json.dumps(changed))
            rejects(lambda: item['select'](private), item['name'] + '/omitted-dependency')
            changed = json.loads(family_bytes)
            helper = private / item['helper']
            helper.write_bytes(original_source + b'\n// forged source family binding\n')
            for row in changed['sources']:
                if row['path'] == item['helper']:
                    row['sha256'] = sha(helper)
            (out / 'family.json').write_text(json.dumps(changed))
            rejects(lambda: item['select'](private), item['name'] + '/forged-source')
            helper.write_bytes(original_source)
            for fields in ([dict(variant='span32-v2')] if item['name'].startswith('hunet') else
                           [dict(variant='reuse-v7'), dict(build_variant='reuse-v8'), dict(build_variant=None)]):
                changed = json.loads(family_bytes)
                changed.update(fields)
                (out / 'family.json').write_text(json.dumps(changed))
                rejects(lambda: item['select'](private), item['name'] + '/family-recipe')
            changed = json.loads(family_bytes)
            changed['legacy']['code_sha256'] = '0' * 64
            (out / 'family.json').write_text(json.dumps(changed))
            rejects(lambda: item['select'](private), item['name'] + '/forged-predecessor')
            if item['name'].startswith('mosaic'):
                changed = json.loads(family_bytes)
                changed['schema_source']['sha256'] = '0' * 64
                (out / 'family.json').write_text(json.dumps(changed))
                rejects(lambda: item['select'](private), item['name'] + '/schema-source-pin')
            (out / 'family.json').write_bytes(family_bytes)
            receipt_path = out / 'rv32-build.json'
            receipt_bytes = receipt_path.read_bytes()
            rec_changes = ([lambda r: r.update(dispatch_variant='span32-v2'),
                            lambda r: r['audited_callback_names'].pop(),
                            lambda r: [c.update(argv=[a.replace('-DVXNN_HUNET_TILE_N=2', '-DVXNN_HUNET_TILE_N=1') for a in c['argv']]) for c in r['commands']]]
                           if item['name'].startswith('hunet') else
                           [lambda r: r.update(variant='reuse-v8'), lambda r: r.update(schedule_version=1)])
            for change in rec_changes:
                changed_receipt = json.loads(receipt_bytes)
                change(changed_receipt)
                receipt_path.write_text(json.dumps(changed_receipt))
                changed = json.loads(family_bytes)
                changed['program_receipt_sha256'] = sha(receipt_path)
                (out / 'family.json').write_text(json.dumps(changed))
                rejects(lambda: item['select'](private), item['name'] + '/receipt-recipe')
            receipt_path.write_bytes(receipt_bytes)
            (out / 'family.json').write_bytes(family_bytes)
            assert item['select'](private) == out
        assert all(sha(path) == digest for path, digest in legacy.items())
    print(json.dumps(dict(scope='R13_REAL_BUILD_PUBLICATION_AND_NEGATIVE_IDENTITIES_NOT_GPU_PASS',
                          families=2, rejected_corruptions=rejected, gpu_raw_cycles=None), indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

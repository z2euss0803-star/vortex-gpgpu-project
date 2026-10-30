"""Default routing and real provenance helpers, without model/image generation."""
import argparse
import hashlib
import json
import pathlib
import shutil
import sys
import tempfile
import unittest


ROOT = BASELINE = WORKSPACE = None


class GlobalProgramTests(unittest.TestCase):
    def test_defaults_admit_new_actual_programs(self):
        from scripts.unified import hunet_program as h, mosaic_program as m
        self.assertEqual(h.CURRENT, 'hunet-fp16-v3')
        self.assertEqual(m.CURRENT, 'mosaic-int16-v8')
        self.assertEqual(h.selected_name(ROOT), 'hunet-fp16-v3')
        self.assertEqual(m.resolve_variant(), 'reuse-v8')
        self.assertEqual(m.selected_name(ROOT), 'mosaic-int16-v8')
        self.assertEqual(m.resolve_requested_variant({}), 'reuse-v8')

    def test_historical_selection_stays_explicit_and_rejects_source_drift(self):
        from scripts.unified import hunet_program as h, mosaic_program as m
        self.assertEqual(h.selected_name(BASELINE, variant='span32-v2'), 'hunet-fp16-v2')
        self.assertEqual(m.selected_name(BASELINE, variant='reuse-v7'), 'mosaic-int16-v7')
        for select, variant in ((h.selected_name, 'span32-v2'), (m.selected_name, 'reuse-v7')):
            with self.subTest(variant=variant), self.assertRaisesRegex(ValueError, 'PUBLISHED_GPU_SOURCE_DRIFT'):
                select(ROOT, variant=variant)
        for variant in ('span32-tile1-v3', 'span32-tile2-v4', True):
            with self.subTest(variant=variant), self.assertRaises(ValueError):
                h.selected_name('/invalid-root', variant=variant)

    def test_explicit_mosaic_branches_and_conflicts(self):
        from scripts.unified.mosaic_program import resolve_variant as resolve, resolve_requested_variant
        for name in ('pack32-ro64-v3', 'fused-pointwise-v4', 'cooperative-v6', 'reuse-v7', 'reuse-v8'):
            self.assertEqual(resolve(name), name)
        self.assertEqual(resolve(fused=False), 'pack32-ro64-v3')
        self.assertEqual(resolve(fused=True), 'fused-pointwise-v4')
        self.assertEqual(resolve(cooperative=False), 'pack32-ro64-v3')
        self.assertEqual(resolve(fused=True, cooperative=True), 'cooperative-v6')
        for args in (dict(variant='reuse-v8', fused=False), dict(variant='reuse-v8', cooperative=True),
                     dict(variant='reuse-v9'), dict(variant=True), dict(fused=1), dict(cooperative=True)):
            with self.subTest(args=args), self.assertRaises(ValueError):
                resolve(**args)
        for env, kwargs in (({'mosaic_variant': 'reuse-v7'}, dict(variant='reuse-v8')),
                            ({'mosaic_variant': 'reuse-v8'}, dict(fused=False)),
                            ({'mosaic_variant': 'reuse-v8', 'mosaic_fused': True}, dict(fused=False))):
            with self.subTest(env=env), self.assertRaises(ValueError):
                resolve_requested_variant(env, **kwargs)

    def test_joint_firmware_default_has_new_code_in_all_16_entries(self):
        from scripts.unified.bind_kernel import firmware_inputs
        firmware = firmware_inputs(ROOT, 4)
        self.assertEqual(len(firmware['entries']), 16)
        entries = {(e['namespace'], e['logical_profile'], e['logical_program']): e for e in firmware['entries']}
        self.assertEqual(hashlib.sha256(entries[(1, 1, 65552)]['payload'][:65536]).hexdigest(),
                         '8cb3e18144d60a05e57009d23d02f0da8d07d9741b1bb780d4a0d4fa144990a6')
        self.assertEqual(hashlib.sha256(entries[(1, 1, 65568)]['payload'][:65536]).hexdigest(),
                         '618632402b615385dd165f7fa25a4526843cb7e975517a3c7fb16d27435fa34c')
        self.assertIsNone(firmware['gpu_raw_cycles'])

    def test_profile_and_workspace_policy_preserve_abi4(self):
        from scripts.unified.image import mosaic_payload_policy
        from scripts.unified import generate_mosaic_package as generator
        policy = getattr(generator, 'program_workspace_policy', None)
        self.assertTrue(callable(policy), 'GENERATOR_WORKSPACE_POLICY_REQUIRED')
        self.assertEqual(mosaic_payload_policy({}), dict(variant='reuse-v8', fusion=True, pointwise_budget=4096, profiling_options=0))
        self.assertEqual(mosaic_payload_policy({'mosaic_profiling_options': 1})['variant'], 'reuse-v8')
        self.assertEqual(policy('reuse-v8', 1), dict(version=1, abi=4, layout_version=1, profile_wire_version=2,
                         prefix_bytes=64, conv_raw_bytes=384, core_profile_bytes=256, schedule_version=2,
                         weight_layout_version=2, profiling_options=1))
        self.assertEqual(policy('reuse-v7', 1), policy('reuse-v8', 1))
        self.assertEqual(policy('cooperative-v6', 0)['abi'], 3)
        for env in ({'mosaic_profiling_options': True}, {'mosaic_profiling_options': 2},
                    {'mosaic_pointwise_budget': 0}, {'mosaic_pointwise_budget': True},
                    {'mosaic_variant': 'pack32-ro64-v3', 'mosaic_profiling_options': 1}):
            with self.subTest(env=env), self.assertRaises(ValueError):
                mosaic_payload_policy(env)
        for variant, options in (('reuse-v8', True), ('reuse-v8', 2), ('pack32-ro64-v3', 1), ('unknown', 0)):
            with self.subTest(variant=variant, options=options), self.assertRaises(ValueError):
                policy(variant, options)

    def test_real_provenance_hashes_active_pins_and_changes_with_pin_bytes(self):
        from scripts.unified import generate_hunet_package as h, generate_mosaic_package as m
        cases = ((h, {}, 'include/vxnn/hunet_tile2_program.hpp',
                  '41e92079107e3b7318ac9131602de3130413ef6d822bc1c265cfe597406de57b',
                  'scripts/unified/hunet_tile2_program.py'),
                 (m, dict(variant='reuse-v8'), 'include/vxnn/mosaic_reuse_v8_program.hpp',
                  'fe9ac129a8800f1cf3e32d89fc0d27db41cef11906cb848247a8a94f104de802',
                  'scripts/unified/mosaic_reuse_v8_program.py'))
        for generator, kwargs, pin, expected_pin_sha, module in cases:
            rows_fn = getattr(generator, 'implementation_source_rows', None)
            self.assertTrue(callable(rows_fn), 'GENERATOR_REAL_PROVENANCE_HELPER_REQUIRED')
            rows = rows_fn(ROOT, **kwargs)
            by_path = {r['path']: r for r in rows}
            self.assertEqual(len(rows), len(by_path))
            self.assertEqual(by_path[pin]['sha256'], expected_pin_sha)
            self.assertIn(module, by_path)
            for row in rows:
                self.assertEqual(row['sha256'], hashlib.sha256((ROOT / row['path']).read_bytes()).hexdigest())
            with tempfile.TemporaryDirectory(dir=WORKSPACE, prefix='provenance665-') as tmp:
                private = pathlib.Path(tmp)
                for path in by_path:
                    target = private / path
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(ROOT / path, target)
                altered = private / pin
                altered.write_bytes(altered.read_bytes() + b'\n// real provenance mutation\n')
                changed = {r['path']: r for r in rows_fn(private, **kwargs)}
                self.assertNotEqual(changed[pin]['sha256'], expected_pin_sha)
                self.assertEqual(changed[pin]['sha256'], hashlib.sha256(altered.read_bytes()).hexdigest())
        hrows = {r['path'] for r in h.implementation_source_rows(ROOT, program_variant='span32-v2')}
        self.assertIn('include/vxnn/hunet_span_program.hpp', hrows)
        self.assertNotIn('include/vxnn/hunet_tile2_program.hpp', hrows)
        mrows = {r['path'] for r in m.implementation_source_rows(ROOT, variant='reuse-v7')}
        self.assertIn('scripts/unified/mosaic_reuse_program.py', mrows)
        self.assertNotIn('scripts/unified/mosaic_reuse_v8_program.py', mrows)


def main():
    global ROOT, BASELINE, WORKSPACE
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', required=True)
    p.add_argument('--baseline', required=True)
    p.add_argument('--workspace', required=True)
    args = p.parse_args()
    ROOT, BASELINE, WORKSPACE = map(lambda path: pathlib.Path(path).resolve(), (args.root, args.baseline, args.workspace))
    WORKSPACE.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(ROOT))
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(GlobalProgramTests))
    print(json.dumps(dict(scope='R13_GLOBAL_ROUTING_AND_PROVENANCE_NO_MODEL_OR_GPU', tests=result.testsRun,
                          failures=len(result.failures), errors=len(result.errors), gpu_raw_cycles=None)))
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    raise SystemExit(main())

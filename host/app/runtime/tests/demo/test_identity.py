import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from scripts.demo.identity import verify_sources
from scripts.demo.run_stage import run_stage


class Admission(unittest.TestCase):
    def setUp(self):
        Path('build/demo/tmp').mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(dir='build/demo/tmp')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        (self.root/'input.bin').write_bytes(b'original')
        self.manifest = self.root/'manifest.json'
        self.data = dict(version=1, files=[dict(path='input.bin', role='production', bytes=8,
             sha256=hashlib.sha256(b'original').hexdigest())], programs=[dict(id=12, app='mosaic')])
        self.save()

    def save(self):
        self.manifest.write_text(json.dumps(self.data), encoding='utf-8')

    def test_source_byte_drift_rejected(self):
        self.assertEqual(verify_sources(str(self.manifest))['semantic'], 'SOURCE_IDENTITY_MATCH_NOT_RUNTIME_PASS')
        (self.root/'input.bin').write_bytes(b'originaX')
        with self.assertRaisesRegex(ValueError, 'identity'):
            verify_sources(str(self.manifest))

    def test_old_program_ids_collision_rejected(self):
        for number in range(1, 12):
            self.data['programs'][0]['id'] = number
            self.save()
            with self.assertRaises(ValueError):
                verify_sources(str(self.manifest))

    def test_existing_run_dir_not_overwritten(self):
        out = self.root/'run'; out.mkdir()
        (out/'stdout.txt').write_text('original evidence')
        with self.assertRaises(FileExistsError):
            run_stage('identity', str(self.manifest), str(out))
        self.assertEqual((out/'stdout.txt').read_text(), 'original evidence')

    def test_field_stage_disabled(self):
        for name in ('board', 'sd', 'field', 'reset', 'pl'):
            with self.assertRaises(PermissionError):
                run_stage(name, str(self.manifest), str(self.root/name))
            self.assertFalse((self.root/name).exists())

    def test_unknown_stage_never_passes(self):
        result = run_stage('not-implemented', str(self.manifest), str(self.root/'unknown'))
        self.assertNotEqual(result['exit'], 0)
        self.assertEqual(result['semantic'], 'NOT_IMPLEMENTED')

    def test_source_escape_and_duplicate_rejected(self):
        for path in ('../input.bin', '/input.bin', 'C:/input.bin'):
            self.data['files'][0]['path'] = path; self.save()
            with self.assertRaises(ValueError): verify_sources(str(self.manifest))
        self.data['files'][0]['path'] = 'input.bin'
        self.data['files'].append(self.data['files'][0]); self.save()
        with self.assertRaises(ValueError): verify_sources(str(self.manifest))

    def test_stage_receipt_keeps_natural_failure(self):
        env = self.root/'env.json'
        env.write_text(json.dumps({'source_manifest':str(self.manifest)}))
        out = self.root/'identity'
        result = run_stage('identity', str(env), str(out))
        self.assertEqual(result['exit'], 0)
        self.assertIsNone(result['gpu_raw_cycles'])
        self.assertTrue((out/'receipt.json').is_file())


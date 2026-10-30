"""Real generator -> secure loader -> prepare_mosaic admission, HOST only.

Set MOSAIC_BATCH_FIXTURE to a frozen full64 package, MOSAIC_BATCH_HOST to
image_package_metadata, and MOSAIC_BATCH_EVIDENCE to a NEW result directory.
The fixture oracle uses the original package's item counts, independently of
the generator's implementation. No transport or device is opened.
"""
import copy
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import unittest

from scripts.unified.generate_mosaic_package import generate


def wire(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':')).encode()


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


@unittest.skipUnless(all(os.environ.get(k) for k in (
    'MOSAIC_BATCH_FIXTURE', 'MOSAIC_BATCH_HOST', 'MOSAIC_BATCH_EVIDENCE')),
    'requires immutable full Mosaic fixture and real HOST preparation probe')
class MosaicBatchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture = Path(os.environ['MOSAIC_BATCH_FIXTURE'])
        cls.host = Path(os.environ['MOSAIC_BATCH_HOST']).resolve()
        cls.out = Path(os.environ['MOSAIC_BATCH_EVIDENCE'])
        cls.out.mkdir(parents=True, exist_ok=False)
        cls.base = {p.name: p.read_bytes() for p in cls.fixture.iterdir()}
        cls.binding = json.loads(cls.base['binding.json'])
        cls.graph = json.loads(cls.base['graph.json'])
        assert digest(cls.base['weights-original.ms8']) == 'f10ffc08bf9a2f89fb6561999938c24ddaebb56af255875a42652fde65acf973'
        assert digest(cls.base['input.f32']) == 'b972b9c52b7d16359331bd12fd504653ed71d307ebbf31fb6c58b0f70f1479c0'
        assert cls.binding['budget'] == 64 and len(cls.binding['nodes']) == 77
        assert sum(n['job_count'] for n in cls.graph['nodes']) == 99251
        # Explicitly derived test manifest from the exact frozen blob. It is
        # NOT represented as an independently obtained original source manifest.
        blob = cls.base['weights-original.ms8']
        manifest = dict(version=1, format='MS8AST1', bytes=len(blob), sha256=digest(blob),
                        source_sha256={k: blob[48+i*32:80+i*32].hex() for i, k in enumerate(
                            ('model', 'selected', 'source_old', 'checkpoint'))},
                        layers=[dict(zip(('co', 'ci', 'k', 'norm'),
                            (struct.unpack_from('<8I', blob, 256+i*384)[j] for j in (1, 2, 3, 6))))
                            for i in range(16)])
        cls.assets_manifest = cls.out/'derived-weight-manifest.json'
        cls.assets_manifest.write_bytes(wire(manifest))
        (cls.out/'fixture-identities.json').write_bytes(wire(dict(
            scope='EXACT_FROZEN_PACKAGE_HOST_ONLY',
            files={p: digest(raw) for p, raw in cls.base.items()},
            manifest_note='derived from exact SHA frozen blob; not original external manifest',
            derived_weight_manifest_sha256=digest(wire(manifest)), gpu_raw_cycles=None)))

    def package(self, name, binding=None, graph=None, maxima=None):
        """Reseal content-valid semantic attacks; never modify frozen input."""
        path = self.out/name
        path.mkdir()
        data = dict(self.base)
        if binding is not None:
            data['binding.json'] = wire(binding)
        if graph is not None:
            data['graph.json'] = wire(graph)
        manifest = json.loads(data['manifest.json'])
        manifest.pop('model_id')
        if maxima:
            manifest.update(maxima)
        for row in manifest['assets']:
            raw = data[row['path']]
            row.update(bytes=len(raw), sha256=digest(raw))
        manifest['model_id'] = digest(wire(manifest))
        data['manifest.json'] = wire(manifest)
        for name, raw in data.items():
            member = path/name
            member.write_bytes(raw)
            member.chmod(0o400)
        path.chmod(0o500)
        return path

    def probe(self, name, path):
        command = [str(self.host), 'mosaic', str(path), '/usr/share/vxgpu/models/mosaic']
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
        (self.out/(name+'.log')).write_bytes(result.stdout)
        (self.out/(name+'.receipt.json')).write_bytes(wire(dict(
            command=command, natural_exit=result.returncode, output_sha256=digest(result.stdout),
            scope='ACTUAL_HOST_PREPARE_NOT_GPU', gpu_raw_cycles=None)))
        return result

    def policy_fixture(self, pointwise=4096):
        """Independent contract fixture; no production scheduling helper."""
        binding, graph = copy.deepcopy(self.binding), copy.deepcopy(self.graph)
        binding['batch_policy'] = dict(version=1, convolution_budget=64, pointwise_budget=pointwise)
        for audit, node in zip(binding['nodes'], graph['nodes']):
            budget = 64 if audit['opcode'] == 2 else pointwise
            jobs = (audit['items']+budget-1)//budget
            audit.update(budget=budget, jobs=jobs, tail_items=audit['items']-(jobs-1)*budget)
            node['parameters']['budget'] = budget
            node.update(job_count=jobs, evidence_bytes=4096+jobs*512)
        return binding, graph

    def maxima(self, graph):
        return dict(max_jobs=sum(n['job_count'] for n in graph['nodes']),
                    max_evidence_bytes=sum(n['evidence_bytes'] for n in graph['nodes']))

    def test_generator_separates_pointwise_and_convolution_and_preserves_legacy(self):
        args = (self.fixture/'weights-original.ms8', self.assets_manifest,
                self.fixture/'input.f32', 1280, 960, 64)
        old = self.out/'generated-legacy64'
        legacy = generate(*args, old, provider_flag=self.binding['hardware']['provider_flags'])
        self.assertEqual(legacy['jobs'], 99251)
        self.assertNotIn('batch_policy', json.loads((old/'binding.json').read_bytes()))
        new = self.out/'generated-pointwise4096-conv64'
        try:
            split = generate(*args, new, provider_flag=self.binding['hardware']['provider_flags'], pointwise_budget=4096)
        except TypeError as error:
            self.fail('explicit pointwise budget is missing: '+str(error))
        self.assertEqual(split['jobs'], 58291)
        b = json.loads((new/'binding.json').read_bytes())
        g = json.loads((new/'graph.json').read_bytes())
        self.assertEqual(b['batch_policy'], dict(version=1, convolution_budget=64, pointwise_budget=4096))
        self.assertEqual(g['tensors'], self.graph['tensors'])
        self.assertEqual(g['inputs'], self.graph['inputs'])
        self.assertEqual(g['outputs'], self.graph['outputs'])
        self.assertEqual(len(g['nodes']), 77)
        self.assertEqual(b['nodes'][0]['tail_items'], 2112)
        self.assertEqual(b['nodes'][1]['jobs'], 9600)
        for audit, actual, original in zip(b['nodes'], g['nodes'], self.graph['nodes']):
            with self.subTest(phase=audit['phase']):
                for key in ('id', 'inputs', 'outputs', 'program_requirements'):
                    self.assertEqual(actual[key], original[key])
                self.assertEqual({k: v for k, v in actual['parameters'].items() if k != 'budget'},
                                 {k: v for k, v in original['parameters'].items() if k != 'budget'})
                self.assertEqual(actual['parameters']['budget'], 64 if audit['opcode'] == 2 else 4096)
                self.assertEqual(actual['job_count'], audit['jobs'])
                self.assertEqual(actual['evidence_bytes'], 4096+audit['jobs']*512)
                self.assertEqual((audit['jobs']-1)*audit['budget']+audit['tail_items'], audit['items'])
        for member, raw in self.base.items():
            if member not in ('binding.json', 'graph.json', 'manifest.json'):
                self.assertEqual((new/member).read_bytes(), raw)
        for package in (old, new):
            for member in package.iterdir():
                member.chmod(0o400)
            package.chmod(0o500)
        legacy_result = self.probe('generated-legacy', old)
        split_result = self.probe('generated-split', new)
        self.assertEqual(legacy_result.returncode, 0, legacy_result.stdout.decode())
        self.assertEqual(split_result.returncode, 0, split_result.stdout.decode())
        a, z = json.loads(legacy_result.stdout), json.loads(split_result.stdout)
        self.assertNotEqual(a['installed_model']['model'], z['installed_model']['model'])
        self.assertNotEqual(a['installed_model']['plan'], z['installed_model']['plan'])

    def test_real_prepare_accepts_independent_split_contract_and_old_package(self):
        old = self.probe('original-legacy64', self.fixture)
        self.assertEqual(old.returncode, 0, old.stdout.decode())
        binding, graph = self.policy_fixture()
        new = self.package('independent-split', binding, graph, self.maxima(graph))
        result = self.probe('independent-split', new)
        self.assertEqual(result.returncode, 0, result.stdout.decode())
        prepared = json.loads(result.stdout)
        self.assertEqual((prepared['nodes'], prepared['gpu_jobs']), (77, 58291))
        self.assertIsNone(prepared['gpu_raw_cycles'])

    def test_real_prepare_rejects_content_valid_malformed_policy_and_cardinality(self):
        cases = (
            'version', 'version-bool', 'unknown-policy-field', 'missing-policy-budget',
            'null-policy', 'zero-budget', 'oversize-budget', 'bool-budget',
            'convolution-mismatch', 'missing-policy', 'node-budget', 'job-count',
            'audit-jobs', 'audit-tail', 'audit-phase', 'audit-missing', 'evidence',
            'max-jobs', 'max-evidence', 'unknown-binding-field',
        )
        for case in cases:
            with self.subTest(case=case):
                binding, graph = self.policy_fixture(64 if case in (
                    'version', 'version-bool', 'unknown-policy-field', 'missing-policy-budget',
                    'null-policy', 'zero-budget', 'oversize-budget', 'bool-budget',
                    'convolution-mismatch', 'unknown-binding-field') else 4096)
                policy = binding['batch_policy']
                if case == 'version': policy['version'] = 2
                if case == 'version-bool': policy['version'] = True
                if case == 'unknown-policy-field': policy['deadline_ns'] = 60000000001
                if case == 'missing-policy-budget': del policy['pointwise_budget']
                if case == 'null-policy': binding['batch_policy'] = None
                if case == 'zero-budget': policy['pointwise_budget'] = 0
                if case == 'oversize-budget': policy['pointwise_budget'] = 4097
                if case == 'bool-budget': policy['pointwise_budget'] = True
                if case == 'convolution-mismatch': policy['convolution_budget'] = 32
                if case == 'missing-policy': del binding['batch_policy']
                if case == 'node-budget': graph['nodes'][0]['parameters']['budget'] = 64
                if case == 'job-count': graph['nodes'][0]['job_count'] -= 1
                if case == 'audit-jobs': binding['nodes'][0]['jobs'] -= 1
                if case == 'audit-tail': binding['nodes'][0]['tail_items'] -= 1
                if case == 'audit-phase': binding['nodes'][0]['phase'] = 1
                if case == 'audit-missing': binding['nodes'].pop()
                if case == 'evidence': graph['nodes'][0]['evidence_bytes'] -= 1
                if case == 'unknown-binding-field': binding['unchecked_policy'] = 1
                maxima = self.maxima(graph)
                if case == 'max-jobs': maxima['max_jobs'] += 1
                if case == 'max-evidence': maxima['max_evidence_bytes'] += 1
                path = self.package('mutant-'+case, binding, graph, maxima)
                result = self.probe('mutant-'+case, path)
                self.assertNotEqual(result.returncode, 0, 'malformed contract accepted: '+case)
                self.assertIn('MOSAIC_PLAN:', result.stdout.decode())


if __name__ == '__main__':
    unittest.main(verbosity=2)

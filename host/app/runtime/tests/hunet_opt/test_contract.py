"""Reject malformed closed graph envelopes before any device operation."""
import copy
import importlib.util
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[2]


def fixture():
    # Independent little endian wire fixture: GN_STATS -> GN_APPLY.
    nodes = [[4, 7, 1, 1, 0x85000040, 0, 0x85000200, 0, 0, 0, 0, 0,
              1, 1, 8, 8, 8, 8, 1, 1, 0, 1, 1, 0],
             [5, 8, 2, 2, 0x85000040, 0, 0x85000300, 0, 0, 0x80800000,
              0x80800040, 0x85000200, 1, 1, 8, 8, 8, 8, 1, 1, 0, 1, 1, 0]]
    header = [0x32444748, 2, 320, 1, 1, 2, 7, 8, 2, 2, 0x86c4df40, 4096] + [0]*20
    descriptor = struct.pack('<32I', *header) + b''.join(struct.pack('<24I', *n) for n in nodes)
    job = struct.pack('<32I', *([0x32474c48, 2, 128, 16, 1, 2, 7, 8,
                               1, 0x84000000, 320, 0x86c4df40, 4096, 0, 0, 2] + [0]*16))
    return job, descriptor


class ContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.path = ROOT / 'scripts/hunet_opt/contract.py'

    def module(self):
        self.assertTrue(self.path.is_file(), 'missing optimization contract validator')
        spec = importlib.util.spec_from_file_location('opt_contract', self.path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_sizes_and_wire_vectors(self):
        m = self.module()
        c = m.load_contract(ROOT / 'contracts/hunet_opt_v2.json')
        self.assertEqual((c['binding_bytes'], c['job_bytes'], c['request_bytes'],
                          c['cpu_evidence_bytes']), (512, 128, 176, 384))
        job, desc = fixture()
        result = m.validate_graph(job, desc)
        self.assertEqual((result['block_id'], result['first'], result['last']), (1, 1, 2))
        self.assertEqual(m.descriptor_bytes(1), 256)
        self.assertEqual(m.descriptor_bytes(32), 3200)
        bad = copy.deepcopy(c)
        bad['program_id'] = 9
        with self.assertRaises(ValueError):
            m.validate_contract(bad)

    def test_bad_graph_envelope(self):
        m = self.module()
        job, desc = fixture()
        for word, value in [(0, 0), (1, 1), (2, 127), (3, 18), (4, 0),
                            (5, 1024), (8, 0), (8, 65), (9, 0x84001000),
                            (10, 4097), (11, 0), (12, 1), (14, 262145),
                            (15, 1), (16, 1), (31, 1)]:
            words = list(struct.unpack('<32I', job)); words[word] = value
            with self.subTest(job_word=word, value=value), self.assertRaises(ValueError):
                m.validate_graph(struct.pack('<32I', *words), desc)
        for word, value in [(0, 0), (1, 1), (2, 256), (3, 2), (8, 33),
                            (9, 1), (12, 1), (31, 1), (32, 6),
                            (36, 0xfffffff0), (34, 2), (56, 4), (58, 1)]:
            words = list(struct.unpack('<80I', desc)); words[word] = value
            with self.subTest(descriptor_word=word, value=value), self.assertRaises(ValueError):
                m.validate_graph(job, struct.pack('<80I', *words))
        # One-node descriptor has 32 bytes of zero alignment padding.
        j = list(struct.unpack('<32I', job)); j[3] = 17; j[5] = 1; j[7] = 7; j[10] = 256
        h = list(struct.unpack('<32I', desc[:128])); h[2] = 256; h[5] = 1; h[7] = 7; h[8] = 1
        d = struct.pack('<32I', *h) + desc[128:224] + bytes(32)
        m.validate_graph(struct.pack('<32I', *j), d)
        with self.assertRaises(ValueError):
            m.validate_graph(struct.pack('<32I', *j), d[:-1]+b'\x01')


if __name__ == '__main__':
    unittest.main()

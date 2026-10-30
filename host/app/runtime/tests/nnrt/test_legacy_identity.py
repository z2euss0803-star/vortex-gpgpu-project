import json
from pathlib import Path
import re
import unittest
from scripts.nnrt.verify_candidate import verify_candidate

ROOT = Path(__file__).resolve().parents[2]


class LegacyIdentityTests(unittest.TestCase):
    def test_old_uapi_device_and_generated_184_unchanged(self):
        frozen = json.loads((ROOT / 'docs/runtime/legacy-identity.json').read_text(encoding='utf-8'))
        self.assertEqual(len(frozen['files']), 184)
        report = verify_candidate(ROOT, frozen)
        self.assertTrue(report['admitted'], report['errors'])

    def test_generic_ioctl_namespace_free(self):
        numbers = []
        for path in (ROOT / 'include/uapi').glob('*.h'):
            numbers += [int(n, 0) for n in re.findall(r"_IO(?:WR|W|R)?\(\s*'V'\s*,\s*(0[xX][0-9a-fA-F]+|\d+)", path.read_text())]
        self.assertEqual(set(numbers), set(range(1, 23)) | set(range(64, 70)))
        self.assertFalse(set(numbers) & set(range(0x80, 0x8c)))
        self.assertEqual(len(numbers), len(set(numbers)))


if __name__ == '__main__':
    unittest.main()

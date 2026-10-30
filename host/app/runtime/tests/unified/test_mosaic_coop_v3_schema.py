import hashlib
import os
import pathlib
import struct
import subprocess
import sys
import unittest
SOURCE = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(SOURCE / 'scripts' / 'unified'))
from mosaic_schema import make_schema
from mosaic_schema_coop_v3 import make_schema_coop_v3

class CoopSchemaTest(unittest.TestCase):
    def test_actual_c_catalog_relocation_and_target_binding(self):
        out = pathlib.Path(os.environ['COOP_SCHEMA_OUTPUT'])
        new = make_schema_coop_v3()
        self.assertEqual(len(new), 1536)
        self.assertEqual(new, make_schema_coop_v3())
        self.assertNotEqual(new, make_schema(fused=True))
        packet = out / 'coop-v3-schema.bin'
        with packet.open('xb') as file:
            file.write(new)
        result = subprocess.run([os.environ['COOP_SCHEMA_C_BINARY'], str(packet)], capture_output=True, check=False)
        (out / 'c-binding.stdout').write_bytes(result.stdout)
        (out / 'c-binding.stderr').write_bytes(result.stderr)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
        self.assertIn(b'ACTUAL_C_CATALOG_RELOCATION_TARGET_BINDING_HOST_PASS', result.stdout)
        print('schema_sha256=' + hashlib.sha256(new).hexdigest())
    def test_exact_abi3_and_options_wire_matches_packet_offsets(self):
        raw = make_schema_coop_v3()
        rows = [struct.unpack('<16I', raw[i:i+64]) for i in range(64, len(raw), 64)]
        scalars = {r[1]: r for r in rows if r[0] == 1}
        self.assertEqual(scalars[0][11:13], (3, 3))
        self.assertEqual(scalars[124][11:13], (0, 1))
        self.assertEqual(scalars[116][11:13], (1, 4096))
        self.assertFalse(any(r[0] == 3 for r in rows))
        addresses = [r for r in rows if r[0] == 2]
        self.assertEqual([r[1] for r in addresses], [16,20,24,28,32,36,40,44])
        self.assertEqual([r[6] for r in addresses], [48,52,56,60,64,68,72,76])
        self.assertEqual([r[3] for r in addresses], [1,2,3,1,1,3,3,3])
        self.assertEqual([r[4] for r in addresses], [4,4,2,2,4,4,4,64])

if __name__ == '__main__':
    unittest.main(verbosity=2)

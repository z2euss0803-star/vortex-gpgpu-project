import importlib.util,unittest
from pathlib import Path
p=Path(__file__).resolve().parents[1]/'scripts/prepare_ops.py'
s=importlib.util.spec_from_file_location('prep',p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
class TestFixture(unittest.TestCase):
    def test_real_pack_zero_required(self):
        self.assertEqual(m.validate_pack({0x81020040:0,0x81020044:0},[dict(buffer_byte_address='0x81020040',pack_values=2)]),2)
        with self.assertRaises(ValueError):m.validate_pack({0x81020040:0,0x81020044:1},[dict(buffer_byte_address='0x81020040',pack_values=2)])
    def test_missing_pack_word_rejected(self):
        with self.assertRaises(ValueError):m.validate_pack({0x81020040:0},[dict(buffer_byte_address='0x81020040',pack_values=2)])
    def test_exact_little_endian_job(self):
        import struct
        values=list(range(32)); mem={0x10010000+4*i:v for i,v in enumerate(values)}
        self.assertEqual(m.job_bytes(mem,0x10010000),struct.pack('<32I',*values))
        with self.assertRaises(ValueError):m.job_bytes({},0x10010000)
    def test_cdma_ranges_preserve_gaps(self):
        self.assertEqual(m.contiguous_ranges([0x80000000,0x80000004,0x80000010]),[(0x80000000,0x80000008),(0x80000010,0x80000014)])
if __name__=='__main__':unittest.main()

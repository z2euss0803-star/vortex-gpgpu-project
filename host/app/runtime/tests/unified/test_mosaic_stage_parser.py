import pathlib
import struct
import unittest
from scripts.unified.analyze_mosaic_stages import analyze_bytes

FIXTURE=pathlib.Path("G:/s8_software_r5_20261028/evidence/a-r8-extended-trace-497/mixed-trace.bin")
if not FIXTURE.exists():
    FIXTURE=pathlib.Path("/mnt/g/s8_software_r5_20261028/evidence/a-r8-extended-trace-497/mixed-trace.bin")
class Stages(unittest.TestCase):
    def data(self):
        data=bytearray(FIXTURE.read_bytes())
        struct.pack_into("<Q",data,512+16,8)
        return data
    def test_mixed_raw_records(self):
        report=analyze_bytes(self.data())
        self.assertEqual((report["jobs"],report["profiled_jobs"],report["bytes"]),(2,1,2048))
        self.assertEqual(report["scopes"],["HOST_PROTOCOL_TEST"])
        self.assertFalse(report["cross_core_sum_as_elapsed"])
        self.assertIsNone(report["derived_elapsed_seconds"])
        self.assertEqual([x["stages"] for x in report["per_core"]],[{},{},{},{}])
    def test_per_core_aggregation_is_kept_separate(self):
        data=self.data()
        for core,cycles in ((0,10),(1,40)):
            record=512+512+core*256
            struct.pack_into("<I",data,record+52,1)
            struct.pack_into("<4Q",data,record+64,cycles,2,4,1)
        report=analyze_bytes(data)
        self.assertEqual(report["per_core"][0]["stages"]["input_pack"]["cycles"],10)
        self.assertEqual(report["per_core"][1]["stages"]["input_pack"]["cycles"],40)
        self.assertIsNone(report["derived_elapsed_seconds"])
    def test_zero_csr_with_work_is_unavailable(self):
        data=self.data();record=512+512
        struct.pack_into("<I",data,record+52,1)
        struct.pack_into("<4Q",data,record+64,0,2,4,1)
        with self.assertRaisesRegex(ValueError,"CSR"):
            analyze_bytes(data)
    def test_mutated_geometry(self):
        data=self.data();data[512+128+84]^=1
        with self.assertRaisesRegex(ValueError,"GEOMETRY"):
            analyze_bytes(data)
    def test_core_and_tag_and_version(self):
        for offset in (512+512+20,512+512+60,512+512+4):
            data=self.data();data[offset]^=1
            with self.assertRaises(ValueError):analyze_bytes(data)
    def test_no_silent_truncation_or_empty_pass(self):
        for data in (b"",self.data()[:-1],self.data()[:511]):
            with self.assertRaises(ValueError):analyze_bytes(data)
    def test_no_silent_unknown_format_or_duplicate_job(self):
        data=self.data();data[0]=0
        with self.assertRaises(ValueError):analyze_bytes(data)
        data=FIXTURE.read_bytes()
        with self.assertRaisesRegex(ValueError,"INDEX"):analyze_bytes(data)
if __name__=="__main__":unittest.main()

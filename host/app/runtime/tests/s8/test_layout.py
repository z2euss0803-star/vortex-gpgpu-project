from pathlib import Path
import copy,importlib,json,unittest
ROOT=Path(__file__).resolve().parents[2]

class LayoutTests(unittest.TestCase):
    def api(self):
        try:return importlib.import_module('scripts.s8.layout')
        except ModuleNotFoundError:self.fail('production fixed S8 layout missing')
    def elf(self):
        return dict(entry=0x80000000,load_ranges=[dict(start=0x80000000,file_bytes=4096,mem_bytes=8192,writable=False)],tls_base=0x80002000,tbss_bytes=4,tbss_stride=16,tbss_align=16,tdata_bytes=0,static_stack_bytes=4096)
    def test_full_map_ps_reserve_and_overflow(self):
        api=self.api();catalog=json.loads((ROOT/'contracts/s8/cases.json').read_text())
        result=api.build_layout(self.elf(),catalog)
        self.assertEqual(result['objects_count'],53)
        self.assertEqual(result['regions']['STACK'],[0xfff70000,0xffff0040])
        self.assertEqual(result['ps_reserved']['CRT_IO'],[0x10002000,0x10003000])
        self.assertEqual(result['ps_reserved']['FENCE'],[0x10030000,0x10030040])
        self.assertEqual(result['tls_ranges'][-1],[0x800023f0,0x800023f4])
        for key,value in [('entry',0x10000000),('tdata_bytes',4),('static_stack_bytes',8128),('tls_base',0x800efff0),('tbss_stride',3),('tbss_align',3)]:
            bad=self.elf();bad[key]=value
            with self.assertRaises(ValueError,msg=key):api.build_layout(bad,catalog)
        bad=self.elf();bad['load_ranges'][0]['mem_bytes']=2**64
        with self.assertRaises(ValueError):api.build_layout(bad,catalog)
    def test_guard_gap_and_wrong_slot(self):
        api=self.api()
        self.assertEqual(api.guard_byte(3,0,0,0),0xa6)
        self.assertEqual(api.guard_byte(3,1,1,63),0xa5^3^1^0x3d^63)
        initial=api.initial_image(5,7)
        self.assertEqual(len(initial),4096)
        self.assertEqual(initial[64:448],b'\xcc'*384)
        self.assertEqual(initial[448:1216],bytes(768))
        self.assertEqual(initial[1728:2240],bytes(512))
        self.assertEqual(initial[2240:2304],b'\x6d'*64)
        for offset in (0,2240,4032):
            bad=bytearray(initial);bad[offset]^=1
            self.assertFalse(api.verify_only_changes(initial,bytes(bad),[(64,384)]),offset)
        bad=bytearray(initial);bad[64]^=1
        self.assertTrue(api.verify_only_changes(initial,bytes(bad),[(64,384)]))
        self.assertFalse(api.verify_only_changes(initial,api.initial_image(5,6),[]))
        self.assertEqual(len(api.initial_image(3,0,65536)),131072)
        with self.assertRaises(ValueError):api.initial_image(3,0,131072)
        with self.assertRaises(ValueError):api.verify_only_changes(initial,initial,[(4095,2)])

if __name__=='__main__':unittest.main()

from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[2]
class Device(unittest.TestCase):
    def test_actual_entry_contract(self):
        s=(ROOT/'device/hunet_opt.cpp').read_text()
        self.assertIn('hunet_opt_gn_run',s);self.assertIn('opt_gn_phases',s)
        self.assertIn('vx_spawn_threads',s);self.assertIn('vx_barrier(token,cores)',s)
        self.assertIn('fence iorw, iorw',s)
        self.assertIn('HunetJobV1 local=*j',s)
        self.assertIn('opt_find_descriptor',s);self.assertIn('opt_pack_row',s)
        self.assertIn('HUNET_OPT_LOCATE',s);self.assertIn('HUNET_OPT_PACK',s)
        h=(ROOT/'device/hunet_opt_gn_runtime.h').read_text()
        self.assertEqual(h.count('opt_gn_join(p);'),6)
        self.assertIn('p.fence();p.barrier(0x80000000,4)',h)
if __name__=='__main__':unittest.main()

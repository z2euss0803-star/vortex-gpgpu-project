import copy
import importlib.util
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('opt_layout',ROOT/'scripts/hunet_opt/layout.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Layout(unittest.TestCase):
    def test_exact_offsets_and_rejections(self):
        v=m.gn_layout();m.validate_layout(v)
        self.assertEqual([x['offset'] for x in v['gn']['regions']],[0,64,320,576,832,896,1024,4032])
        for offset in [63,65,319,4033]:
            bad=copy.deepcopy(v);bad['gn']['regions'][1]['offset']=offset
            with self.assertRaises(ValueError):m.validate_layout(bad)
        bad=copy.deepcopy(v);bad['gn']['bytes']=4097
        with self.assertRaises(ValueError):m.validate_layout(bad)
        v['timing']=m.timing_layout();m.validate_layout(v)
        v['timing']['start']+=64
        with self.assertRaises(ValueError):m.validate_layout(v)
if __name__=='__main__':unittest.main()

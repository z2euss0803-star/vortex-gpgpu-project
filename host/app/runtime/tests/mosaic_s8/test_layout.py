import copy,unittest
from scripts.mosaic.contract import build_graph
from scripts.mosaic.layout import plan_layout,check_layout
from tests.mosaic_s8.test_contract import MODEL
class LayoutTests(unittest.TestCase):
    def test_live_range_no_overlap_full_image(self):
        graph=build_graph(MODEL);layout=plan_layout(graph)
        self.assertIsInstance(layout,dict)
        self.assertTrue(check_layout(layout))
        roles={x['role']:x for x in layout['regions']}
        self.assertEqual(roles['code']['address'],0x80000000)
        self.assertEqual(roles['weights']['address'],0x80800000)
        self.assertEqual(roles['fp_a']['bytes'],157286400)
        self.assertEqual(roles['fp_b']['bytes'],157286400)
        self.assertEqual(roles['q_input']['bytes'],78643200)
        self.assertEqual(roles['skip']['bytes'],39321600)
        self.assertEqual(roles['norm_tree']['bytes'],4*2097152*8)
        self.assertLess(layout['end'],0xfff00000)
        self.assertEqual(layout['scope'],'STATIC_CAPACITY_NOT_MEASURED_PEAK')
        self.assertEqual(roles['skip']['lifetime'],[3,12])
        regions=layout['regions'][2:]
        for a,b in zip(regions,regions[1:]):
            self.assertLessEqual(a['address']+a['bytes']+64,b['address']-64)
    def test_unbounded_tile_or_address_domain_rejected(self):
        layout=plan_layout(build_graph(MODEL))
        for key,value in [('address',0x100000000),('address',0xfffffffffffffff0),
                          ('address',0xfff00000),('bytes',1<<64),('guard',0)]:
            bad=copy.deepcopy(layout);bad['regions'][2][key]=value
            with self.subTest(key=key,value=value),self.assertRaises(ValueError):check_layout(bad)
        bad=copy.deepcopy(layout);bad['regions'][3]['address']=bad['regions'][2]['address']
        with self.assertRaises(ValueError):check_layout(bad)
    def test_small_shapes_and_residual_liveness(self):
        layout=plan_layout(build_graph(MODEL,12,8))
        self.assertTrue(check_layout(layout))
        self.assertLess(layout['dynamic_bytes'],1000000)
        self.assertEqual(len({r['role'] for r in layout['regions']}),len(layout['regions']))

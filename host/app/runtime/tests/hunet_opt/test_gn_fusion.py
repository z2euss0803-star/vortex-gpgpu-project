import copy
import unittest
from scripts.hunet_opt.catalog import compile_catalog,validate_catalog
from scripts.hunet_opt.graph import fold_norm,gn_pair_can_fuse
from scripts.hunet_opt.contract import validate_graph
import struct
class Norm(unittest.TestCase):
    def test_all_sixteen_pairs_closed(self):
        before=compile_catalog('G1');after=compile_catalog('G3_NORM');validate_catalog(after)
        self.assertEqual((after['gpu_jobs'],after['cpu_jobs'],after['logical_jobs'],after['stages']),(87,13,1023,100))
        pairs=[e for e in after['entries'] if e['op']==16];self.assertEqual(len(pairs),16)
        self.assertEqual([e['first'] for e in before['entries'] if e['kind']=='CPU'],[e['first'] for e in after['entries'] if e['kind']=='CPU'])
        for entry in pairs:
            self.assertEqual([n[0] for n in entry['nodes']],[4,5])
            slot=after['graph_slots'][entry['block_id']-1]
            validate_graph(struct.pack('<32I',*entry['words']),bytes.fromhex(slot['hex']))
    def test_wrong_gamma_alias_or_extra_consumer_retained(self):
        entries=compile_catalog('G1')['entries'];index=next(i for i,e in enumerate(entries) if e['op']==4)
        self.assertTrue(gn_pair_can_fuse(entries,index))
        for field in (11,12,13,14):
            bad=copy.deepcopy(entries);bad[index+1]['words'][field]=bad[index]['words'][8] if field==13 else bad[index+1]['words'][field]+64
            self.assertFalse(gn_pair_can_fuse(bad,index))
        bad=copy.deepcopy(entries);extra=copy.deepcopy(bad[index+1]);extra['words'][3]=8;extra['op']=8;extra['words'][8]=bad[index]['words'][13];bad.insert(index+2,extra)
        self.assertFalse(gn_pair_can_fuse(bad,index))
if __name__=='__main__':unittest.main()

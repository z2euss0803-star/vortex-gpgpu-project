import copy
import unittest
from pathlib import Path
import tempfile
import hashlib
from scripts.hunet_opt.generate import emit_artifacts
from scripts.hunet_opt.catalog import compile_catalog,validate_catalog
from scripts.hunet_opt.graph import view_window_can_fold,assemble_graph
class Views(unittest.TestCase):
    def test_actual_closed_windows_and_mapping(self):
        baseline=compile_catalog('G3_NORM');candidate=compile_catalog('G3_VIEWS');validate_catalog(candidate)
        folded=[e for e in candidate['entries'] if e['op']==17]
        self.assertTrue(folded)
        self.assertEqual(candidate['gpu_jobs'],baseline['gpu_jobs']-4*len(folded))
        self.assertEqual([e['first'] for e in candidate['entries'] if e['kind']=='CPU'],[e['first'] for e in baseline['entries'] if e['kind']=='CPU'])
        self.assertTrue(all(len(e['nodes'])==5 and e['owner_tiles']<=262144 for e in folded))
        self.assertTrue(candidate['view_report']['saved_materialization_bytes']>0)
        self.assertEqual(len(candidate['view_report']['retained_shift_stages']),8)
        for e in folded:
            self.assertEqual(len(e['virtual_stages']),3)
            self.assertTrue(e['view_sources'])
    def test_external_consumer_alias_and_node_bounds(self):
        es=compile_catalog('G3_NORM')['entries'];i=next(i for i,e in enumerate(es) if e['op']==12)
        self.assertTrue(view_window_can_fold(es,i))
        bad=copy.deepcopy(es);extra=copy.deepcopy(bad[i+4]);extra['words'][8]=bad[i]['words'][13];bad.insert(i+5,extra)
        self.assertFalse(view_window_can_fold(bad,i))
        block=next(e for e in compile_catalog('G3_VIEWS')['entries'] if e['op']==17)
        bad=copy.deepcopy(block);bad['nodes']*=7
        with self.assertRaises(ValueError):assemble_graph([bad])
        bad=copy.deepcopy(block);bad['owner_tiles']=262145
        with self.assertRaises(ValueError):assemble_graph([bad])
        with self.assertRaises(ValueError):assemble_graph([copy.deepcopy(block) for _ in range(65)])
        bad=copy.deepcopy(es);bad[i+2]['words'][11]=bad[i]['words'][8]
        self.assertFalse(view_window_can_fold(bad,i))
        bad=copy.deepcopy(es);bad[i+2]['kind']='CPU'
        self.assertFalse(view_window_can_fold(bad,i))
    def test_generated_view_wire_is_deterministic(self):
        c=compile_catalog('G3_VIEWS');parent=Path('build/opt-views-deterministic');parent.mkdir(exist_ok=True)
        a=Path(tempfile.mkdtemp(dir=str(parent)));b=Path(tempfile.mkdtemp(dir=str(parent)));emit_artifacts(c,a);emit_artifacts(c,b)
        self.assertEqual({p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in a.iterdir()},{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in b.iterdir()})
if __name__=='__main__':unittest.main()

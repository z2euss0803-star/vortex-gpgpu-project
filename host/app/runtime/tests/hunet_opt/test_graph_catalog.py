import copy
import hashlib
from pathlib import Path
import tempfile
import unittest
import struct
import subprocess
from scripts.hunet_opt.catalog import compile_catalog,validate_catalog,validate_submission
from scripts.hunet_opt.generate import emit_artifacts
ROOT=Path(__file__).resolve().parents[2]
class Catalog(unittest.TestCase):
    def test_base_candidates_and_deterministic_generation(self):
        for candidate in ('C1_T','C1_H','G1','G2_LOCATE','G2_PACK','G2_WMMA2','G2_WMMA4'):
            cat=compile_catalog(candidate);validate_catalog(cat)
            self.assertEqual((cat['gpu_jobs'],cat['cpu_jobs'],cat['logical_jobs'],cat['stages']),(103,13,1023,100))
            coverage=[q for e in cat['entries'] for q in range(e['first'],e['last']+1)]
            self.assertEqual(coverage,list(range(1,1024)))
            parent=ROOT/'build/opt-gen-test';parent.mkdir(exist_ok=True)
            a=Path(tempfile.mkdtemp(dir=str(parent)));b=Path(tempfile.mkdtemp(dir=str(parent)))
            emit_artifacts(cat,a);emit_artifacts(cat,b)
            self.assertEqual({p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in a.iterdir()},{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in b.iterdir()})
    def test_catalog_and_submission_mutations_rejected(self):
        cat=compile_catalog('G1')
        for key,value in [('first',0),('last',1024),('first_stage',100)]:
            bad=copy.deepcopy(cat);bad['entries'][0][key]=value
            with self.assertRaises(ValueError):validate_catalog(bad)
        for change in ('duplicate','missing','cpu','pointer'):
            bad=copy.deepcopy(cat)
            if change=='duplicate':bad['entries'].insert(1,copy.deepcopy(bad['entries'][0]))
            elif change=='missing':bad['entries'].pop(0)
            elif change=='cpu':bad['entries'][0]['kind']='CPU'
            else:bad['entries'][0]['words'][8]+=64
            with self.assertRaises(ValueError):validate_catalog(bad)
        first=next(e for e in cat['entries'] if e['kind']=='GPU');validate_submission(cat,first['words'],1)
        for word in range(32):
            bad=list(first['words']);bad[word]^=1
            with self.assertRaises(ValueError):validate_submission(cat,bad,1)
        with self.assertRaises(ValueError):validate_submission(cat,first['words'],2)
        for key,value in [('block_id',65),('owner_tiles',262145)]:
            bad=copy.deepcopy(cat);bad['entries'][0][key]=value
            with self.assertRaises(ValueError):validate_catalog(bad)
    def test_c_python_same_wire_mutation_corpus(self):
        binary=ROOT/'build/test_opt_validate'
        if not binary.is_file():self.skipTest('C executable requires Ubuntu build; A static tests only')
        from test_contract import fixture
        from scripts.hunet_opt.contract import validate_graph
        parent=ROOT/'build/opt-parity';parent.mkdir(exist_ok=True);out=Path(tempfile.mkdtemp(dir=str(parent)))
        job,descriptor=fixture();vectors=[(job,descriptor)]
        for which,source in [(0,job),(1,descriptor)]:
            for i in range(len(source)//4):
                words=list(struct.unpack('<%dI'%(len(source)//4),source));words[i]^=1;mut=struct.pack('<%dI'%len(words),*words)
                vectors.append((mut,descriptor) if which==0 else (job,mut))
        for j,d in vectors:
            try:validate_graph(j,d);expected=0
            except ValueError:expected=1
            (out/'job').write_bytes(j);(out/'descriptor').write_bytes(d)
            actual=subprocess.run([str(binary),'--graph',str(out/'job'),str(out/'descriptor')]).returncode
            self.assertEqual(actual,expected)
if __name__=='__main__':unittest.main()

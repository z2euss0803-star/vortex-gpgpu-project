from pathlib import Path
import copy,importlib,json,tempfile,unittest,zlib
import hashlib

ROOT=Path(__file__).resolve().parents[2]

class ContractTests(unittest.TestCase):
    def api(self,name):
        try:return importlib.import_module('scripts.s8.'+name)
        except ModuleNotFoundError:self.fail('S8 production '+name+' is not implemented')

    def test_fixed_sizes_ids_and_negative_variants(self):
        c=self.api('catalog').load_catalog(ROOT/'contracts/s8/cases.json')
        ids=[r['id'] for r in c['cases']]
        negatives=[(201<<8)|v for v in range(2)]+[(202<<8)]+[(203<<8)|v for v in range(7)]+[(204<<8)|v for v in range(2)]+[(f<<8) for f in (205,206,207)]+[(208<<8)|v for v in range(2)]+[(209<<8)]
        self.assertEqual(sorted(r['id'] for r in c['cases'] if r['kind']=='negative'),sorted(negatives))
        self.assertEqual(len(ids),35)
        self.assertNotIn(201,ids)
        self.assertEqual(zlib.crc32(b'123456789'),0xcbf43926)
        w=json.loads((ROOT/'contracts/s8/wire.json').read_text())
        self.assertEqual((w['job_bytes'],w['result_bytes'],w['binding_bytes']),(128,256,512))
        self.assertEqual((w['profile'],w['program'],w['outer_abi_bytes'],w['abi_major']),(5,11,176,1))
        golden=json.loads((ROOT/'tests/s8/legacy_sha256.json').read_text())
        for path,digest in golden.items():
            self.assertEqual(hashlib.sha256((ROOT/path).read_bytes().replace(b'\r\n',b'\n')).hexdigest(),digest,path)

    def test_schema_rejects_duplicate_unknown_and_wrong_payload(self):
        api=self.api('catalog');base=api.load_catalog(ROOT/'contracts/s8/cases.json')
        for mode in ('duplicate','unknown','empty','payload','negative_variant'):
            data=copy.deepcopy(base)
            if mode=='duplicate':data['cases'].append(copy.deepcopy(data['cases'][0]))
            elif mode=='unknown':data['new_field']=1
            elif mode=='empty':data['cases']=[]
            elif mode=='payload':data['cases'][1]['a_bytes']=255
            else:data['cases'][-1]['id']=(209<<8)|1
            with tempfile.TemporaryDirectory() as d:
                p=Path(d)/'catalog.json';p.write_text(json.dumps(data))
                with self.assertRaises(ValueError,msg=mode):api.load_catalog(p)

    def test_generation_deterministic_and_reserved_contract(self):
        api=self.api('contract')
        with tempfile.TemporaryDirectory() as d:
            a=api.generate(ROOT,Path(d)/'a');b=api.generate(ROOT,Path(d)/'b')
            self.assertEqual(a,b)
            aa={p.name:p.read_bytes() for p in (Path(d)/'a').iterdir()}
            bb={p.name:p.read_bytes() for p in (Path(d)/'b').iterdir()}
            self.assertEqual(aa,bb)
            self.assertEqual(len(a['phase_events']),12)
            self.assertEqual(len(a['binding_digests']),14)
            self.assertEqual(a['reserved_job_offset'],108)
            self.assertEqual(a['reserved_result_offset'],128)

if __name__=='__main__':unittest.main()

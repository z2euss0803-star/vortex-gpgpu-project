import copy,hashlib,json,pathlib,struct,unittest
from scripts.demo.mosaic_catalog import catalog_digests,runtime_digest,make_template,mutable_ranges,template_header,kernel_header
from scripts.demo.mosaic_rv32_contract import plan_runtime
from scripts.mosaic.contract import build_graph
from scripts.mosaic.layout import plan_layout
ROOT=pathlib.Path(__file__).resolve().parents[2]
class Catalog(unittest.TestCase):
    def setUp(self):
        self.model=json.loads((ROOT/'config/mosaic/model-v1.json').read_text())
        self.rows=[]
        for w,h in ((256,256),(1280,960)):
            g=build_graph(self.model,w,h);self.rows.append(dict(graph=g,layout=plan_layout(g)))
    def test_exact_canonical_roots_and_sidecar_extension(self):
        policy='01'*32;layout,graph=catalog_digests(self.rows,policy)
        self.assertEqual(len(bytes.fromhex(layout)),32);self.assertNotEqual(layout,graph)
        self.assertNotEqual(catalog_digests(list(reversed(self.rows)),policy),(layout,graph))
        runtime=plan_runtime(0x9ba0,1312,2096);deps='02'*32;mutable=[dict(offset=0x9000,bytes=0x1000)]
        a=runtime_digest(runtime,mutable,deps,'03'*32)
        b=runtime_digest(runtime,mutable,deps,'04'*32);self.assertNotEqual(a,b)
    def test_reject_unverified_catalog_or_mutable_overlap(self):
        for key in ('graph','layout'):
            bad=copy.deepcopy(self.rows);bad[0][key]['sha256']='05'*32
            with self.assertRaises(ValueError):catalog_digests(bad,'01'*32)
        with self.assertRaises(ValueError):catalog_digests(self.rows*2,'01'*32)
        runtime=plan_runtime(0x9ba0,1312,2096)
        for mutable in ([dict(offset=0,bytes=64)],[dict(offset=64,bytes=128),dict(offset=128,bytes=64)],[dict(offset=0x100000,bytes=64)]):
            with self.assertRaises(ValueError):runtime_digest(runtime,mutable,'02'*32,'03'*32)
    def test_template_has_no_runner_or_binding_cycle(self):
        runtime=plan_runtime(0x9ba0,1312,2096)
        g=self.rows[0]['graph'];m=dict(sha256='06'*32,bytes=g['asset_bytes'],source_sha256=dict(g['sources']),layers=[{k:int(r[k]) for k in ('ci','co','k','norm')} for r in g['layers']])
        receipt=dict(natural_exit=0,scope='ACTUAL_MOSAIC_RV32_LINK_NOT_EXECUTED',runtime=runtime,flat=dict(bytes=39872,sha256='08'*32),rodata=dict(bytes=6307840,gpu=0x80110000,sha256='09'*32),elf=dict(bytes=6400000,sha256='0a'*32))
        # These are structural generator fixtures, never executable release data.
        t=make_template(receipt,self.rows,m,[dict(offset=0x9000,bytes=0x1000)],'01'*32,'02'*32,'0b'*32)
        self.assertNotIn('runner',t);self.assertNotIn('binding',t);self.assertEqual(t['profile'],6);self.assertEqual(t['program'],12)
        self.assertEqual(t['source_sha256'],'0b'*32);self.assertIn('rv32_runtime',t['digests'])
        h=template_header(t);self.assertIn('p->rodata_gpu=2148597760u',h);self.assertNotIn('.binding=',h)
        self.assertIn('#define DEMO_RELEASE_MOSAIC_DEFAULT_LAYOUT 1u',h)
        with self.assertRaises(ValueError):kernel_header(t,bytes(512),b'x',b'y')
        bad=copy.deepcopy(m);bad['layers'][15]['co']=4
        with self.assertRaises(ValueError):make_template(receipt,self.rows,bad,[dict(offset=0x9000,bytes=0x1000)],'01'*32,'02'*32,'0b'*32)
        bad=copy.deepcopy(m);bad['source_sha256']['model']='07'*32
        with self.assertRaises(ValueError):make_template(receipt,self.rows,bad,[dict(offset=0x9000,bytes=0x1000)],'01'*32,'02'*32,'0b'*32)
    def test_tls_template_not_exempted_as_runtime_global(self):
        runtime=plan_runtime(0x9ba0,1312,2096)
        ss=[dict(name='.tbss',flags=0x403,address=0x80009b40,bytes=1312),dict(name='.got',flags=3,address=0x80009b40,bytes=28),dict(name='.sdata',flags=3,address=0x80009b5c,bytes=16),dict(name='.bss',flags=3,address=0x80009b80,bytes=28)]
        ranges=mutable_ranges(ss,runtime)
        self.assertEqual(ranges,[dict(offset=0x9b40,bytes=44),dict(offset=0x9b80,bytes=28),dict(offset=0x9ba0,bytes=83968)])
        ss[1]['flags']=7
        with self.assertRaises(ValueError):mutable_ranges(ss,runtime)
if __name__=='__main__':unittest.main()

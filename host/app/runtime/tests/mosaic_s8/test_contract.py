import copy,json,pathlib,struct,unittest,zlib
from scripts.mosaic.contract import build_graph,encode_job,decode_job,validate_job
ROOT=pathlib.Path(__file__).resolve().parents[2]
MODEL=json.loads((ROOT/'config/mosaic/model-v1.json').read_text())
def fields():
    return dict(run_id=bytes(range(16)),binding_tag=b'B'*16,generation=3,sequence=0,
        op=1,layer=0,tile_first=0,tile_count=14400,core_mask=15,input_epoch=8,
        layout_tag=b'L'*16,graph_tag=b'G'*16,flags=0)
class ContractTests(unittest.TestCase):
    def test_exact_128b_offsets_crc(self):
        f=fields();raw=encode_job(**f)
        self.assertEqual(len(raw),128)
        self.assertEqual(raw[:8],struct.pack('<IHH',0x31434d56,1,128))
        self.assertEqual(raw[8:24],f['run_id']);self.assertEqual(raw[24:40],b'B'*16)
        self.assertEqual(struct.unpack_from('<8I',raw,40),(3,0,1,0,0,14400,15,8))
        self.assertEqual(raw[72:104],b'L'*16+b'G'*16)
        crc=bytearray(raw);crc[104:108]=b'\0'*4
        self.assertEqual(struct.unpack_from('<I',raw,104)[0],zlib.crc32(crc)&0xffffffff)
        self.assertEqual(raw[112:],b'\0'*16);self.assertEqual(decode_job(raw),f)
    def test_corrupt_reserved_stale_and_out_of_bounds_rejected(self):
        f=fields();expected=dict(f,total_tiles=14400)
        self.assertTrue(validate_job(encode_job(**f),expected))
        for name,value in [('generation',2),('sequence',1),('layer',1),('op',2),
                           ('core_mask',1),('tile_first',1),('tile_count',0),
                           ('tile_count',14401),('input_epoch',7),('flags',2),
                           ('binding_tag',b'C'*16),('graph_tag',b'H'*16)]:
            bad=dict(f);bad[name]=value
            with self.subTest(name=name,value=value),self.assertRaises(ValueError):
                validate_job(encode_job(**bad),expected)
        for index in (0,12,104,112,127):
            raw=bytearray(encode_job(**f));raw[index]^=1
            with self.assertRaises(ValueError):decode_job(raw)
        with self.assertRaises(ValueError):decode_job(encode_job(**f)[:-1])
    def test_graph_all16_shapes_residuals_and_tiles(self):
        graph=build_graph(MODEL)
        self.assertIsInstance(graph,dict)
        self.assertEqual(len(graph['layers']),16)
        self.assertEqual(graph['layers'][0]['output_shape'],[32,960,1280])
        self.assertEqual(graph['layers'][2]['output_shape'],[128,240,320])
        self.assertEqual(graph['layers'][15]['output_shape'],[3,960,1280])
        self.assertEqual(graph['layers'][15]['conv_tiles'],153600)
        self.assertEqual(sum(x['save'] for x in graph['layers']),5)
        self.assertEqual(sum(x['residual'] for x in graph['layers']),5)
        for layer in graph['layers']:
            self.assertEqual(layer['K'],layer['ci']*layer['k']**2)
            self.assertEqual(layer['ops'][:2],[1,2])
            self.assertEqual(layer['input_role'],'fp_a' if layer['index']%2==0 else 'fp_b')
        small=build_graph(MODEL,12,8)
        self.assertEqual(small['layers'][-1]['output_shape'],[3,8,12])
        self.assertEqual(small['layers'][2]['conv_tiles'],16)
    def test_shape_and_model_mutations_rejected(self):
        for w,h in ((0,960),(1284,960),(1280,964),(11,8),(8,0)):
            with self.assertRaises(ValueError):build_graph(MODEL,w,h)
        for field,value in [('ci',4),('k',7),('stride',3),('save',True)]:
            m=copy.deepcopy(MODEL);m['layers'][0][field]=value
            with self.assertRaises(ValueError):build_graph(m)
    def test_native_asset_segment_offsets_follow_ms8ast1(self):
        graph=build_graph(MODEL)
        self.assertIn('asset_bytes',graph)
        self.assertEqual(graph['asset_bytes'],3382784)
        self.assertEqual(graph['layers'][0]['weight_segments'][0],dict(kind='q16',offset=6400,bytes=16384))
        for layer in graph['layers']:
            self.assertEqual(len(layer['weight_segments']),5 if layer['norm'] else 3)
            for segment in layer['weight_segments']:self.assertEqual(segment['offset']%64,0)

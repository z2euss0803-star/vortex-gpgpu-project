import hashlib,json,unittest
from pathlib import Path
import numpy as np
from scripts.mosaic.reference import quantize_rne,dot_i64,restore_f32,compensated_sum_f32,global_norm_f32,run_reference


class Numeric(unittest.TestCase):
    def test_norm_uses_full_spatial_population(self):
        x=np.array([[0,0,10,10]],np.float32)
        y=global_norm_f32(x,[1],[0])
        self.assertTrue(np.all(y[0,:2]<-.99));self.assertTrue(np.all(y[0,2:]>.99))

    def test_sixteen_stage_reference_propagates_actual_output(self):
        layers=[];parameters=[];seen=[]
        for i in range(16):
            layers.append(dict(ci=1,co=1,k=3,stride=1,upsample=1,norm=i<15,relu=False,save=False,residual=False))
            w=np.zeros((1,9),np.int16)
            if i==15:w[0,4]=1
            parameters.append(dict(q16=w,coeff=np.array([[1,0]],np.float32),sA=np.array([1],np.float32),norm_gamma=np.array([0],np.float32),norm_beta=np.array([2],np.float32)))
        def stage(i,previous,y):
            if i>0:self.assertTrue(np.all(previous==2))
            seen.append(i)
        y=run_reference(np.arange(16,dtype=np.float32).reshape(1,1,4,4),dict(layers=layers,parameters=parameters),stage)
        self.assertEqual(seen,list(range(16)));self.assertTrue(np.all(y==2))
    def test_compensated_sum_preserves_cancellation_residual(self):
        x=np.array([[1e8,1,-1e8,1],[2,3,4,5]],np.float32)
        self.assertEqual(compensated_sum_f32(x).tolist(),[2.,14.])
        with self.assertRaises(ValueError):compensated_sum_f32(np.array([[]],np.float32))
    def test_rne_ties_and_negative_endpoints(self):
        self.assertEqual(quantize_rne([.5,1.5,-.5,-1.5],1).tolist(),[0,2,0,-2])
        self.assertEqual(quantize_rne([-1e9,1e9],1).tolist(),[-32767,32767])
        for x in (float('nan'),float('inf'),-float('inf')):
            with self.assertRaises(ValueError):quantize_rne([x],1)
        for inverse in (0,-1,float('nan'),float('inf')):
            with self.assertRaises(ValueError):quantize_rne([1],inverse)

    def test_restore_no_fma_each_step(self):
        q=np.array([[16777217]],dtype=np.int64)
        y=restore_f32(q,[2**-24],np.float32(1+2**-23),[-1])
        self.assertEqual(int(y.view(np.uint32)[0,0]),0x34000000)
        self.assertEqual(restore_f32(np.array([[-16777217]],dtype=np.int64),[2**-24],1,[1]).item(),0.)

    def test_i64_full_k_signed_endpoints(self):
        for k in (1,2592,4096):
            a=np.full((1,k),-32768,np.int16);b=np.full((1,k),-32768,np.int16)
            self.assertEqual(dot_i64(a,b).item(),k*1073741824)
            b.fill(32767);self.assertEqual(dot_i64(a,b).item(),-k*1073709056)
        with self.assertRaises(ValueError):dot_i64(np.zeros((1,4097),np.int16),np.zeros((1,4097),np.int16))

    def test_old_layer16_failure_preserved(self):
        source=json.loads(Path('config/demo/source_inputs.json').read_text(encoding='utf-8'))
        row=next(x for x in source['files'] if x['path']=='evidence/G1_bridge_failure_observed_20261008.json')
        path=Path(source['roots'][row['root']])/row['path'];data=path.read_bytes()
        self.assertEqual(hashlib.sha256(data).hexdigest(),row['sha256'])
        old=json.loads(data);self.assertFalse(old['whole_G1_pass']);self.assertEqual(old['exit_code'],1)
        self.assertIsNone(old['board_numeric_pass'])


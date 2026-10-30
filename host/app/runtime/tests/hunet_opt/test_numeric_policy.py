import math,struct,unittest
from scripts.hunet_opt.numeric import compare_outputs
def raw(v):return struct.pack('<8105f',*v)
class Numeric(unittest.TestCase):
    def test_history_metrics_signed_zero_sparse_and_widespread(self):
        old=raw([1.0]*8105);r=compare_outputs(old,dict(r2=old,ort=old),old)
        self.assertEqual(r['history']['fp32_word_difference_count'],0);self.assertEqual(r['history']['rmse'],0)
        x=[1.0]*8105;x[5]=1.125;r=compare_outputs(raw(x),dict(r2=old,ort=old),old)
        self.assertEqual(r['history']['fp32_word_difference_count'],1);self.assertEqual(r['history']['numeric_difference_count'],1);self.assertEqual(r['history']['max_abs'],0.125)
        self.assertFalse(r['prediction']['history_top1_match']);self.assertGreater(r['history']['nrmse'],0)
        z=raw([0.0]*8105);x=[0.0]*8105;x[0]=-0.0;r=compare_outputs(raw(x),dict(r2=z,ort=z),z)
        self.assertEqual(r['history']['fp32_word_difference_count'],1);self.assertEqual(r['history']['numeric_difference_count'],0);self.assertEqual(r['history']['max_abs'],0)
        r=compare_outputs(raw([2.0]*8105),dict(r2=old,ort=old),old);self.assertEqual(r['history']['fp32_word_difference_count'],8105)
    def test_known_historical_failure_is_not_pass(self):
        a=raw([1.0]*8105);r=compare_outputs(a,dict(r2=a,ort=raw([2.0]*8105)),a)
        self.assertEqual(r['classification'],'KNOWN_PRECISION_DEVIATION')
        self.assertEqual(r['r2']['status'],'PASS');self.assertEqual(r['ort']['status'],'FAIL')
        self.assertEqual(r['ort']['mismatch_count'],8105);self.assertTrue(r['history']['byte_identical']);self.assertFalse(r['formal_numeric_pass'])
        self.assertEqual(r['prediction']['top5_indices'],[0,1,2,3,4]);self.assertEqual(r['prediction']['recognition_accuracy'],'UNAVAILABLE')
    def test_boundaries_zero_and_new_differences(self):
        ref=raw([1.0]*8105)
        self.assertEqual(compare_outputs(raw([1.00001]*8105),dict(r2=ref,ort=ref),ref)['r2']['status'],'PASS')
        outside=compare_outputs(raw([1.01]*8105),dict(r2=ref,ort=ref),ref)
        self.assertEqual(outside['classification'],'REVIEW_REQUIRED');self.assertFalse(outside['eligible_for_combine'])
        z=raw([0.0]*8105);r=compare_outputs(z,dict(r2=z,ort=z),z);self.assertEqual(r['r2']['nrmse'],0)
        self.assertEqual(compare_outputs(raw([1e-10]*8105),dict(r2=z,ort=z),z)['r2']['status'],'FAIL')
    def test_nonfinite_shape_and_reference_failures(self):
        a=raw([1.0]*8105)
        for value in (math.inf,math.nan):
            self.assertEqual(compare_outputs(raw([value]*8105),dict(r2=a,ort=a),a)['classification'],'RUNTIME_FAILURE')
        for bad in (None,b'',a[:-4]):
            with self.assertRaises(ValueError):compare_outputs(bad,dict(r2=a,ort=a),a)
        with self.assertRaises(ValueError):compare_outputs(a,dict(r2=a[:-4],ort=a),a)
        with self.assertRaises(ValueError):compare_outputs(a,dict(r2=a,ort=a,r2_contract={'atol':1}),a)
if __name__=='__main__':unittest.main()

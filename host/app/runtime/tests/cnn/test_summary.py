import sys
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'scripts/cnn'))
def record(phase,i,value=999,result=0):
    return dict(scope='TEST_PROTOCOL_NOT_GPU',phase=phase,iteration=i,result=result,
        start_ns=10000000000,end_ns=10000000000+value,bytes=dict(h2d=5000,d2h=1000),
        cnn_audit_end_to_end_ns=value,kernel_chain_host_ns=4,cpu_reference_ns=value,
        tensor_h2d_ns=0,tensor_d2h_ns=0,device_kernel_ns=None,cycles=None,frequency=None,
        layers=[dict(program_id=p,job_id=i*4+p-1,result=0,start_ns=1,end_ns=2,
            layer_submit_wait_ns=1,layer_code_args_h2d_ns=0,layer_verify_ns=0,
            bytes=dict(h2d=4160,d2h=256),guard_pass=True,mismatch_count=0) for p in (2,3,4,5)])
def prefix(): return [record('FIRST',0)]+[record('WARMUP',i) for i in range(3)]
class Summary(unittest.TestCase):
    def load(self):
        self.assertTrue((ROOT/'scripts/cnn/summarize.py').is_file(),'Timing summary missing')
        from summarize import summarize
        return summarize
    def test_twenty(self):
        s=self.load()(prefix()+[record('MEASURED',i,i+1) for i in range(20)])
        self.assertTrue(s['complete']);self.assertFalse(s['real_board_pass'])
        self.assertEqual(s['statistics']['cnn_audit_end_to_end_ns'],dict(n=20,min=1,median=10.5,p95=19,max=20))
        self.assertEqual(s['total'],24);self.assertEqual(s['measured_success'],20)
        self.assertIsNone(s['device_kernel_ns'])
    def test_failure_stops(self):
        records=prefix()+[record('MEASURED',i,i+1) for i in range(6)]+[record('MEASURED',6,7,-5)]
        s=self.load()(records);self.assertFalse(s['complete']);self.assertEqual(s['measured_success'],6);self.assertEqual(s['failures'],1)
        s=self.load()(records+[record('MEASURED',7,8)]);self.assertIn('continued_after_failure',s['errors'])
    def test_corruption(self):
        records=prefix()+[record('MEASURED',i,i+1) for i in range(20)];summarize=self.load()
        for change in ('clock','cycles','phase','guard','sum','missing'):
            import copy
            bad=copy.deepcopy(records)
            if change=='clock': bad[-1]['end_ns']=0
            if change=='cycles': bad[-1]['cycles']=100
            if change=='phase': bad[-1]['phase']='WARMUP'
            if change=='guard': bad[-1]['layers'][0]['guard_pass']=False
            if change=='sum': bad[-1]['kernel_chain_host_ns']=5
            if change=='missing': del bad[-1]['cpu_reference_ns']
            self.assertFalse(summarize(bad)['complete'],change)
if __name__=='__main__':unittest.main()

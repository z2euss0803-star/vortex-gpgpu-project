import sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import audit_ops

class TestProcessGate(unittest.TestCase):
    def setUp(self):
        self.root=Path('F:/work/candidate');self.snapshot='hw4_mosaic_soc_v1'
        self.claim=dict(pid=123,started='2026-10-06T14:00:01+08:00')
        self.start=dict(query_success=True,target_count=1,worker_pid=123,snapshot=self.snapshot,sampled='2026-10-06T14:00:02+08:00',
            processes=[dict(ProcessId=123,Name='python.exe',CreationDate='2026-10-06T14:00:00+08:00',
                CommandLine='D:/PYTHON/python.exe -B F:/work/candidate/scripts/run_ops.py')])
        self.exit=dict(query_success=True,target_count=0,worker_pid=123,snapshot=self.snapshot,sampled='2026-10-06T15:00:00+08:00',processes=[])
        self.start['tracking_scope']=self.exit['tracking_scope']='worker_snapshot_descendants'
        self.exit['global_heavy_count']=0
        self.exit['receipts']=list(self.start['processes'])
    def check(self):
        return audit_ops.require_process_exit(self.exit,self.claim,self.start,self.snapshot,self.root,'run_ops.py')
    def test_default_query_cannot_prove_target_exit(self):
        self.exit.update(worker_pid=0,snapshot='')
        with self.assertRaises(AssertionError):self.check()
    def test_wrong_worker_creation_or_command_rejected(self):
        self.start['processes'][0]['CommandLine']='python other.py'
        with self.assertRaises(AssertionError):self.check()
    def test_pid_reuse_is_not_absence(self):
        self.exit['processes']=[dict(self.start['processes'][0],CreationDate='2026-10-06T14:59:00+08:00')]
        with self.assertRaises(AssertionError):self.check()
    def test_real_bound_absence_accepted(self):self.check()
    def test_cim_equivalent_timestamp_formats_accepted(self):
        self.exit['receipts']=[dict(self.start['processes'][0],CreationDate='2026-10-06T14:00:00.0000000+08:00')]
        self.check()
if __name__=='__main__':unittest.main()

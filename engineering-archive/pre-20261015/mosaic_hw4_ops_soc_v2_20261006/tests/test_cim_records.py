import copy,datetime,importlib.util,sys,unittest
from pathlib import Path
PIPE=Path(__file__).resolve().parents[2]/'mosaic_board_v1_20261004/evidence/hw4_soc_execution_20261006_v2'
def normalize(q):
    p=PIPE/'process_records.py'
    if not p.exists():return copy.deepcopy(q)
    s=importlib.util.spec_from_file_location('records',p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
    return m.normalize_legacy_start(q)
class TestCimRecords(unittest.TestCase):
    def setUp(self):
        self.q=dict(processes=[dict(ProcessId=37424,CreationDate='/Date(1791268797452)/',CommandLine='python run_baseline.py')],
            receipts=[dict(ProcessId=37424,CreationDate='2026-10-06T14:39:57.4527800+08:00',CommandLine='python run_baseline.py')])
    def test_windows_powershell_precise_receipt_normalizes_legacy_date(self):
        row=normalize(self.q)['processes'][0]
        self.assertEqual(datetime.datetime.fromisoformat(row['CreationDate']),datetime.datetime.fromisoformat(self.q['receipts'][0]['CreationDate']))
    def test_missing_receipt_rejected(self):
        self.q['receipts']=[]
        with self.assertRaises(AssertionError):normalize(self.q)
    def test_mismatching_receipt_millisecond_rejected(self):
        self.q['receipts'][0]['CreationDate']='2026-10-06T14:39:58.4527800+08:00'
        with self.assertRaises(AssertionError):normalize(self.q)
if __name__=='__main__':unittest.main()

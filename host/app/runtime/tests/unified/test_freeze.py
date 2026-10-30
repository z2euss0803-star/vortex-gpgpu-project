"""Freeze exact inputs, never bless mutable work or inherit unrun gates."""
import pathlib,tempfile,unittest
from scripts.unified.freeze import freeze_unified,validate_task_map
SHA='ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad'

class FreezeTests(unittest.TestCase):
    def test_matches_expected_identity_and_refuses_modified_input(self):
        with tempfile.TemporaryDirectory() as d:
            p=pathlib.Path(d)/'input';p.write_bytes(b'abc');out=pathlib.Path(d)/'lock.json'
            row={'path':str(p),'sha256':SHA,'role':'program'}
            result=freeze_unified([row],out)
            self.assertIn('files',result);self.assertEqual(result['files'][0]['sha256'],SHA)
            original=out.read_bytes();p.write_bytes(b'bad')
            with self.assertRaises(ValueError):freeze_unified([row],out)
            self.assertEqual(out.read_bytes(),original)
    def test_unidentified_working_copy_cannot_be_imported(self):
        with tempfile.TemporaryDirectory() as d:
            p=pathlib.Path(d)/'input';p.write_bytes(b'abc')
            with self.assertRaises(ValueError):freeze_unified([{'path':str(p),'role':'untracked_T06'}],pathlib.Path(d)/'out')
    def test_duplicate_and_changed_set_refused(self):
        with tempfile.TemporaryDirectory() as d:
            p=pathlib.Path(d)/'input';p.write_bytes(b'abc');out=pathlib.Path(d)/'out';row={'path':str(p),'sha256':SHA,'role':'program'}
            with self.assertRaises(ValueError):freeze_unified([row,row],out)
            freeze_unified([row],out)
            with self.assertRaises(ValueError):freeze_unified([],out)
    def test_complete_mapping_and_no_false_inherited_PASS(self):
        rows=[{'line':line,'task':i,'mapped_to':['U01'],'status':'PENDING'} for line,n in [('NN',24),('IMAGE',20),('P8',18)] for i in range(1,n+1)]
        self.assertEqual(len(validate_task_map(rows)),62)
        for bad in (rows[:-1],rows+[rows[0]], [{**r,'status':'INHERITED_PASS'} if j==0 else r for j,r in enumerate(rows)]):
            with self.assertRaises(ValueError):validate_task_map(bad)
    def test_inheritance_requires_actual_commit_and_scope(self):
        rows=[{'line':line,'task':i,'mapped_to':['U01'],'status':'PENDING'} for line,n in [('NN',24),('IMAGE',20),('P8',18)] for i in range(1,n+1)]
        row=next(r for r in rows if r['line']=='IMAGE' and r['task']==1)
        row.update(status='INHERITED_PASS',commit='d7a092f62c1db47b2e80f7e0721716712ebc6598',scope='HOST_IDENTITY_ONLY',receipt='verified.json')
        self.assertEqual(len(validate_task_map(rows)),62)

if __name__=='__main__':unittest.main()

import hashlib
import json
import sys
import tempfile
import unittest
import copy
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'scripts/cnn'))
class Release(unittest.TestCase):
    def test_identity_and_false_promotion(self):
        self.assertTrue((ROOT/'scripts/cnn/verify_release.py').is_file(),'Release verifier missing')
        from verify_release import verify_release, make_manifest, REQUIRED_GATES, REQUIRED_SOURCE_FILES
        from generate_fixtures import generate_fixture, CASES
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/'SOURCE_COMMIT').write_text('a'*40+'\n')
            files=list(REQUIRED_SOURCE_FILES)
            for f in files:(root/f).parent.mkdir(parents=True,exist_ok=True);(root/f).write_bytes((ROOT/f).read_bytes())
            for case in CASES:generate_fixture(case,root/'fixtures'/case)
            gates={}
            for g in REQUIRED_GATES:
                for suffix in ('.log','.stderr.txt'):(root/(g+suffix)).write_bytes(b'offline-test-log')
                gates[g]=dict(exit=0,status='PASS',command=['make','test-'+g],cwd=str(root),stdout_sha256=hashlib.sha256(b'offline-test-log').hexdigest(),stderr_sha256=hashlib.sha256(b'offline-test-log').hexdigest())
            receipt=root/'offline.json';receipt.write_text(json.dumps(dict(source_commit='a'*40,source_tree_clean=True,scope='OFFLINE_HOST_ONLY',gates=gates,board='NOT_RUN',h1='NOT_RUN')))
            m=make_manifest(root,'a'*40,files,'fixtures','offline.json')
            self.assertEqual(verify_release(root,m),[])
            original_receipt=receipt.read_bytes()
            for mode in ('no_stdout_sha','no_stderr_sha','no_command','missing_log','changed_log'):
                changed=json.loads(original_receipt);bad=copy.deepcopy(m)
                if mode=='no_stdout_sha':del changed['gates']['cnn-session']['stdout_sha256']
                if mode=='no_stderr_sha':del changed['gates']['cnn-session']['stderr_sha256']
                if mode=='no_command':del changed['gates']['cnn-session']['command']
                log=root/'cnn-session.log'
                if mode=='missing_log':log.unlink()
                if mode=='changed_log':log.write_bytes(b'changed')
                receipt.write_text(json.dumps(changed));bad['offline_receipt']['sha256']=hashlib.sha256(receipt.read_bytes()).hexdigest()
                self.assertTrue(verify_release(root,bad),mode)
                log.write_bytes(b'offline-test-log');receipt.write_bytes(original_receipt)
            for mode in ('program','fixture','commit','length','scope','rtl','board','layer','omission','traversal','gate','spec'):
                bad=copy.deepcopy(m)
                if mode=='program':bad['source_files']['kernel/cnn/id2.bin']='0'*64
                if mode=='fixture':bad['fixtures']['main/x.bin']='0'*64
                if mode=='commit':bad['source_commit']='b'*40
                if mode=='length':bad['programs'][0]['instruction_bytes']=4
                if mode=='scope':bad['scope']='CPU_MOCK_NOT_GPU'
                if mode=='rtl':bad['matrix']['SIMT_RTL']='PASS'
                if mode=='board':bad['matrix']['H0_CNN']='PASS'
                if mode=='layer':bad['programs'].pop()
                if mode=='omission':del bad['source_files']['driver/vxl_hw.c']
                if mode=='traversal':bad['source_files']['../outside']='0'*64
                if mode=='gate':bad['offline_receipt']['sha256']='0'*64
                if mode=='spec':bad['spec_sha256']='0'*64
                self.assertTrue(verify_release(root,bad),mode)
            # Match the modified file hash too: semantic verification must still reject it.
            path=root/'fixtures/main/x.bin'
            original=path.read_bytes();blob=bytearray(original);blob[64]^=1;path.write_bytes(blob)
            bad=copy.deepcopy(m);bad['fixtures']['main/x.bin']=hashlib.sha256(blob).hexdigest()
            self.assertTrue(verify_release(root,bad))
            path.write_bytes(original)
            receipt_data=json.loads(receipt.read_text());receipt_data['gates']['cnn-session']['exit']=1
            receipt.write_text(json.dumps(receipt_data))
            bad=copy.deepcopy(m);bad['offline_receipt']['sha256']=hashlib.sha256(receipt.read_bytes()).hexdigest()
            self.assertTrue(verify_release(root,bad))
            receipt_data['gates']['cnn-session']['exit']=0
            receipt_data['source_tree_clean']=False
            receipt.write_text(json.dumps(receipt_data))
            bad=copy.deepcopy(m);bad['offline_receipt']['sha256']=hashlib.sha256(receipt.read_bytes()).hexdigest()
            self.assertTrue(verify_release(root,bad),'Dirty source cannot claim a sealed commit')
if __name__=='__main__':unittest.main()

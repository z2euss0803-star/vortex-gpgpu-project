import hashlib,json,pathlib,subprocess,tempfile,unittest
from demo.transport import DemoTransport,TransportError
from tests.demo.test_transport import config
class Export(unittest.TestCase):
    def test_download_hashes_raw_and_error_without_compute_result(self):
        with tempfile.TemporaryDirectory(dir='build/demo') as d:
            raw=b'original failure with raw GPU cycles'
            manifest=dict(ok=True,scope='REAL_DRIVER_CONTROL_NOT_COMPUTE_RESULT',state='EXPORT_MANIFEST_READY',run_id='1'*32,files=[dict(name='error-gpu.json',bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest())])
            calls=[]
            def process(argv,**kwargs):
                calls.append(argv)
                if argv[0]=='scp':pathlib.Path(argv[-1]).write_bytes(raw);out=b''
                else:out=json.dumps(manifest).encode()
                return subprocess.CompletedProcess(argv,0,out,b'')
            t=DemoTransport(config(),process=process)
            result=t.export_run({'run_id':'1'*32},pathlib.Path(d)/"中文 ' evidence")
            self.assertEqual(result['state'],'EXPORTED_AND_VERIFIED');self.assertEqual(len(calls),2)
            self.assertIn('/'+'1'*32+'/error-gpu.json',calls[1][-2]);self.assertFalse(result['remote_deleted'])
    def test_traversal_or_truncated_download_is_preserved_failure(self):
        with tempfile.TemporaryDirectory(dir='build/demo') as d:
            manifest=dict(ok=True,scope='REAL_DRIVER_CONTROL_NOT_COMPUTE_RESULT',state='EXPORT_MANIFEST_READY',run_id='1'*32,files=[dict(name='../bad',bytes=3,sha256=hashlib.sha256(b'abc').hexdigest())])
            def process(argv,**kwargs):
                if argv[0]=='scp':pathlib.Path(argv[-1]).write_bytes(b'ab');out=b''
                else:out=json.dumps(manifest).encode()
                return subprocess.CompletedProcess(argv,0,out,b'')
            t=DemoTransport(config(),process=process)
            with self.assertRaises(ValueError):t.export_run({'run_id':'1'*32},pathlib.Path(d)/'unsafe')
            manifest['files'][0]['name']='raw.bin'
            with self.assertRaises(TransportError):t.export_run({'run_id':'1'*32},pathlib.Path(d)/'truncated')
            self.assertTrue((pathlib.Path(d)/'truncated/download-receipt.json').is_file())

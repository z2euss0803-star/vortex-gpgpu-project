import hashlib,json,subprocess,tempfile,unittest
from pathlib import Path
from demo.transport import DemoTransport,TransportError


class Process:
    def __init__(self):self.calls=[];self.fail=None;self.reply={'ok':True,'scope':'MOCK','state':'ASSET_STAGED','raw_cycles':None}
    def __call__(self,argv,**kwargs):
        self.calls.append((argv,kwargs))
        if self.fail is not None:return subprocess.CompletedProcess(argv,self.fail,b'',b'disconnected')
        return subprocess.CompletedProcess(argv,0,json.dumps(self.reply).encode(),b'')


def config():return dict(ssh='ssh',scp='scp',ssh_config='example.conf',host='verified-board',timeout=10)


class Transport(unittest.TestCase):
    def setUp(self):
        Path('build/demo/tmp').mkdir(parents=True,exist_ok=True)
        self.tmp=tempfile.TemporaryDirectory(dir='build/demo/tmp');self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name);self.proc=Process();self.transport=DemoTransport(config(),process=self.proc)

    def asset(self):
        return dict(role='input',bytes=3,sha256=hashlib.sha256(b'abc').hexdigest(),name="中文 '$()` .bin",shape=[3],dtype='U8')

    def test_quoted_chinese_path_no_shell_injection(self):
        path=self.root/"中文 '$()` .bin";path.write_bytes(b'abc');a=self.asset();self.proc.reply['asset_sha256']=a['sha256']
        self.transport.stage(str(path),a)
        scp,ssh=self.proc.calls
        self.assertFalse(scp[1]['shell']);self.assertFalse(ssh[1]['shell'])
        self.assertEqual(ssh[0][-1],'/usr/bin/vxl_demo stage --request-stdin')
        self.assertNotIn(a['name'],ssh[0][-1]);self.assertEqual(json.loads(ssh[1]['input'])['name'],a['name'])
        self.assertTrue(scp[0][-1].endswith(a['sha256']+'.part'))

    def test_scp_without_sftp_supported(self):
        p=self.root/'plain';p.write_bytes(b'abc');a=self.asset();self.proc.reply['asset_sha256']=a['sha256']
        self.transport.stage(str(p),a)
        self.assertIn('-O',self.proc.calls[0][0])

    def test_truncated_upload_not_finalized(self):
        p=self.root/'short';p.write_bytes(b'ab')
        with self.assertRaises(ValueError):self.transport.stage(str(p),self.asset())
        self.assertEqual(self.proc.calls,[])
        p.write_bytes(b'abc');self.proc.fail=1
        with self.assertRaises(TransportError):self.transport.stage(str(p),self.asset())
        self.assertEqual(len(self.proc.calls),1)

    def test_metadata_control_characters_and_host_options_rejected(self):
        for host in ('-oProxyCommand=bad','x;poweroff','x\r\ny'):
            with self.assertRaises(ValueError):DemoTransport(dict(config(),host=host),process=self.proc)
        a=self.asset();a['name']='x\r\ny'
        with self.assertRaises(ValueError):self.transport.stage('unused',a)
        self.assertFalse(self.proc.calls)

    def test_no_auto_retry_oversize_and_duplicate_reply_rejected(self):
        self.proc.fail=255
        with self.assertRaises(TransportError):self.transport.call('identity',{})
        self.assertEqual(len(self.proc.calls),1)
        self.proc.fail=None
        with self.assertRaises(ValueError):self.transport.call('status',{'metadata':'x'*(256*1024)})
        def duplicate(argv,**kwargs):return subprocess.CompletedProcess(argv,0,b'{"ok":true,"ok":false}',b'')
        with self.assertRaises(TransportError):DemoTransport(config(),process=duplicate).call('identity',{})


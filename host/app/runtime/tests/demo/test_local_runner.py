import base64,hashlib,json,os,socket,subprocess,tempfile,time,unittest
from pathlib import Path


class LocalRunner(unittest.TestCase):
    def test_export_retains_error_files_even_without_compute_result(self):
        run='3'*32;directory=self.root/run;directory.mkdir(mode=0o700)
        raw=b'original GPU failure and partial evidence';(directory/'error-gpu.json').write_bytes(raw);(directory/'error-gpu.json').chmod(0o600)
        q=self.call('export',{'run_id':run});self.assertEqual(q.returncode,0,q.stdout)
        m=json.loads(q.stdout);self.assertEqual(m['state'],'EXPORT_MANIFEST_READY');self.assertEqual(m['run_id'],run)
        self.assertEqual(m['files'],[dict(name='error-gpu.json',bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest())])
        self.assertEqual((directory/'error-gpu.json').read_bytes(),raw)
        (directory/'bad-link').symlink_to('/etc/passwd')
        self.assertNotEqual(self.call('export',{'run_id':run}).returncode,0)
        self.assertNotEqual(self.call('export',{'run_id':'../'+run}).returncode,0)
    def setUp(self):
        self.binary=os.environ['DEMO_RUNNER_BINARY']
        self.tmp=tempfile.TemporaryDirectory(prefix='demo-local-')
        self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name);self.sock=str(self.root/'rpc.sock')
        self.server=subprocess.Popen([self.binary,'--serve-local','--store',str(self.root),'--socket',self.sock]+(['--runtime-root',self.runtime_root] if hasattr(self,'runtime_root') else []),stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        self.addCleanup(self.stop)
        deadline=time.monotonic()+3
        while not Path(self.sock).exists():
            if self.server.poll() is not None:raise AssertionError(self.server.stderr.read().decode())
            if time.monotonic()>deadline:raise AssertionError('local socket not ready')
            time.sleep(.01)

    def stop(self):
        if self.server.poll() is None:self.server.terminate()
        self.server.communicate(timeout=3)

    def call(self,action,request):
        encoded=base64.b64encode(json.dumps(request,ensure_ascii=False).encode('utf-8')).decode('ascii')
        return subprocess.run([self.binary,action,'--socket',self.sock,'--request-b64',encoded],stdout=subprocess.PIPE,stderr=subprocess.PIPE)

    def test_second_local_owner_busy_and_identity_no_gpu(self):
        one=self.call('identity',{});self.assertEqual(one.returncode,0)
        result=json.loads(one.stdout);self.assertEqual(result['state'],'ADAPTERS_NOT_CONNECTED');self.assertIsNone(result['raw_cycles'])
        other=subprocess.run([self.binary,'--serve-local','--store',str(self.root),'--socket',str(self.root/'other.sock')],stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=3)
        self.assertNotEqual(other.returncode,0);self.assertIn(b'busy',other.stderr)

    def test_asset_stage_hash_and_truncation(self):
        sha=hashlib.sha256(b'abc').hexdigest();incoming=self.root/'incoming'/(sha+'.part');incoming.write_bytes(b'ab')
        asset=dict(role='input',bytes=3,sha256=sha,name='中文 输入.bin',shape=[3],dtype='U8')
        bad=self.call('stage',asset);self.assertNotEqual(bad.returncode,0)
        self.assertFalse((self.root/'assets'/sha).exists())
        incoming.write_bytes(b'abc');good=self.call('stage',asset)
        self.assertEqual(good.returncode,0,good.stdout);self.assertEqual((self.root/'assets'/sha).read_bytes(),b'abc')

    def test_truncated_socket_request_never_succeeds(self):
        client=socket.socket(socket.AF_UNIX);client.settimeout(3);client.connect(self.sock)
        client.sendall(b'{"action":"identity"');client.shutdown(socket.SHUT_WR)
        result=json.loads(client.recv(4096));client.close()
        self.assertFalse(result['ok']);self.assertEqual(result['state'],'TRUNCATED_REQUEST')

    def test_unknown_action_or_invalid_base64_never_accepted(self):
        self.assertEqual(self.call('poweroff',{}).returncode,2)
        for encoded in ('e30===','e3=0','!!!','YWJj='):
            result=subprocess.run([self.binary,'identity','--socket',self.sock,'--request-b64',encoded],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            self.assertNotEqual(result.returncode,0)

    def test_stdin_bounded_utf8_and_trailing_json_rejected(self):
        argv=[self.binary,'identity','--socket',self.sock,'--request-stdin']
        good=subprocess.run(argv,input=json.dumps({'metadata':'中文'*30000},ensure_ascii=False).encode('utf-8'),stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        self.assertEqual(good.returncode,0,good.stderr)
        for data in (b'{}{}',b'{"x":"\x01"}',b'{"x":"\xff"}',b'{"x":"'+b'x'*(256*1024)+b'"}'):
            bad=subprocess.run(argv,input=data,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            self.assertNotEqual(bad.returncode,0)

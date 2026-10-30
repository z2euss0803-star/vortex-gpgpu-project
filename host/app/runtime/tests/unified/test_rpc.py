"""Exercise the actual Unix socket/parser and U09 scheduler, never a device."""
import errno,json,os,pathlib,select,socket,stat,struct,subprocess,sys,tempfile,time,unittest
class RpcTest(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.temp=tempfile.TemporaryDirectory(prefix='u10-rpc-');cls.path=str(pathlib.Path(cls.temp.name)/'control.sock')
  cls.child=subprocess.Popen([os.environ['U10_RPC_BINARY'],cls.temp.name],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
  if not select.select([cls.child.stdout],[],[],10)[0]:cls.child.terminate();raise AssertionError('U10_RPC_ASSERT server never ready')
  line=cls.child.stdout.readline()
  if line!=b'U10_RPC_READY\n':
   cls.child.wait(timeout=5);raise AssertionError('U10_RPC_ASSERT start: '+repr(line+cls.child.stderr.read()))
 @classmethod
 def tearDownClass(cls):
  cls.child.terminate();cls.child.wait(timeout=5);cls.child.stdout.close();cls.child.stderr.close();cls.temp.cleanup()
 def connect(self):
  s=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);s.settimeout(3);s.connect(self.path);return s
 def exchange(self,raw=None,command='models',**fields):
  if raw is None:raw=json.dumps(dict(version=1,command=command,**fields),separators=(',',':')).encode()
  with self.connect() as s:
   s.sendall(struct.pack('!I',len(raw))+raw)
   def exact(n):
    out=b''
    while len(out)<n:
     x=s.recv(n-len(out));self.assertTrue(x,'unexpected EOF');out+=x
    return out
   n=struct.unpack('!I',exact(4))[0];self.assertLessEqual(n,65536);return json.loads(exact(n).decode())
 def key(self,name):return dict(boot_id='boot',client_id='client',request_id=name)
 def test_01_socket_is_private_and_discovery_does_not_submit(self):
  self.assertEqual(stat.S_IMODE(os.stat(self.path).st_mode),0o600)
  for cmd in ('models','scenes','effects'):
   out=self.exchange(command=cmd);self.assertTrue(out['ok']);self.assertIsNone(out['result']['gpu_raw_cycles'])
  missing=self.exchange(command='status',**self.key('not-submitted'));self.assertEqual(missing['error']['code'],errno.ENOENT)
 def test_02_strict_envelope_cannot_switch_mode_or_send_gpu_descriptors(self):
  before=self.exchange()['result']['control_calls']
  bad=[b'{"version":1,"version":1,"command":"models"}',b'{"version":1,"command":"models","mode":"legacy-compat"}',b'{"version":1.0,"command":"models"}',b'{"version":1,"command":"models","fd":4}',b'{"version":1,"command":"models"}\x00',b'{"version":1,"command":"reset"}']
  for raw in bad:self.assertFalse(self.exchange(raw=raw)['ok'])
  after=self.exchange()['result']['control_calls'];self.assertEqual(after,before+1)
 def test_03_same_key_dedup_changed_payload_and_reboot_staleness(self):
  fields=dict(self.key('dedup'),entry_id='fixture',input_id='fixed',parameters={})
  for _ in range(2):self.assertEqual(self.exchange(command='submit',**fields)['result']['status'],'QUEUED')
  fields['parameters']={'variant':1};out=self.exchange(command='submit',**fields);self.assertEqual(out['error']['code'],errno.EEXIST)
  fields['boot_id']='old-boot';out=self.exchange(command='submit',**fields);self.assertEqual(out['error']['code'],errno.ESTALE)
  out=self.exchange(command='cancel',**self.key('dedup'));self.assertEqual(out['result']['status'],'CANCELLED')
 def test_04_disconnect_does_not_cancel_or_execute(self):
  payload=json.dumps(dict(version=1,command='submit',entry_id='fixture',input_id='fixed',parameters={},**self.key('disconnect'))).encode()
  with self.connect() as s:s.sendall(struct.pack('!I',len(payload))+payload);s.shutdown(socket.SHUT_WR)
  time.sleep(.05)
  state=self.exchange(command='status',**self.key('disconnect'));self.assertEqual(state['result']['status'],'QUEUED');self.assertIsNone(state['result']['gpu_raw_cycles'])
  self.assertTrue(self.exchange(command='cancel',**self.key('disconnect'))['ok'])
 def test_05_oversize_partial_and_truncated_frame_do_not_poison_server(self):
  before=self.exchange()['result']['control_calls']
  for frame in (struct.pack('!I',65537),struct.pack('!I',10)+b'{}',b'\x00\x01'):
   with self.connect() as s:s.sendall(frame);s.shutdown(socket.SHUT_WR);s.recv(1024)
  self.assertEqual(self.exchange()['result']['control_calls'],before+1)
 def test_06_parameters_and_paths_are_not_a_raw_gpu_interface(self):
  base=dict(self.key('paths'),entry_id='fixture',input_id='fixed',parameters={})
  for field,value in (('input_id','../../dev/vortex0'),('entry_id','/tmp/custom.elf'),('parameters',{'x':{'address':268435456}}),('parameters',{'x':[1,2]})):
   fields=dict(base);fields[field]=value;self.assertFalse(self.exchange(command='submit',**fields)['ok'])
 def test_07_real_control_cli_discovery_submit_status_cancel_and_no_retry(self):
  def cli(*args):return subprocess.run([sys.executable,'tools/vxgpuctl.py','--socket',self.path]+list(args),stdout=subprocess.PIPE,stderr=subprocess.PIPE)
  p=cli('models');self.assertEqual(p.returncode,0,p.stderr);self.assertTrue(json.loads(p.stdout.decode())['ok'])
  for cmd,extra in [('submit',['--entry-id','fixture','--input-id','fixed']),('status',[]),('cancel',[])]:
   p=cli(cmd,'--boot-id','boot','--client-id','client','--request-id','cli',*extra);self.assertEqual(p.returncode,0,p.stderr)
  p=cli('status','--boot-id','boot','--client-id','client','--request-id','cli');self.assertEqual(json.loads(p.stdout.decode())['result']['status'],'CANCELLED')
  p=cli('models','--mode','compute-v2');self.assertEqual(p.returncode,2)
 def test_08_bounded_handler_duration_does_not_consume_reply_transport_deadline(self):
  out=self.exchange(command='export',artifact=0,destination_id='host-slow-file-check',**self.key('slow-file'))
  self.assertTrue(out['ok']);self.assertTrue(out['result']['checked']);self.assertIsNone(out['result']['gpu_raw_cycles'])
if __name__=='__main__':unittest.main(verbosity=2)

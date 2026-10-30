"""B controller: one native Mosaic -> original file -> pinned texture -> P8 DP.

Requires an already started, identity-audited S8 joint board service. It does
not start/reboot/reconfigure a board, resubmit inference, or reuse old images.
"""
import argparse,hashlib,json,os,pathlib,re,shlex,struct,subprocess,sys,time
from scripts.unified.joint_conversion import convert,verify_conversion,wire,digest

class ServiceError(RuntimeError):
 def __init__(self,row):self.row=row;self.code=row.get('code');super().__init__(str(row))

# Fresh fixed inbox, manifest last. Neither a shell path nor archive members
# are supplied by RPC; the trusted B receipt binds every transferred byte.
UPLOAD=r'''
import os,sys,json,struct,hashlib,stat
root='/run/vxgpud/texture-inbox';leaf=sys.argv[1]
assert len(leaf)==64 and all(c in '0123456789abcdef' for c in leaf)
parent='/run/vxgpud';s=os.lstat(parent);assert stat.S_ISDIR(s.st_mode) and s.st_uid==os.geteuid() and stat.S_IMODE(s.st_mode)==0o700
try:os.mkdir(root,0o700)
except FileExistsError:pass
s=os.lstat(root);assert stat.S_ISDIR(s.st_mode) and s.st_uid==os.geteuid() and stat.S_IMODE(s.st_mode)==0o700
def exact(n):
 b=b''
 while len(b)<n:
  v=sys.stdin.buffer.read(min(n-len(b),1048576));assert v;b+=v
 return b
n=struct.unpack('!I',exact(4))[0];assert 0<n<=65536;rows=json.loads(exact(n));assert 1<=len(rows)<=16 and rows[-1]['name']=='manifest.json'
destination=root+'/'+leaf;os.mkdir(destination,0o700);total=0;names=set()
for row in rows:
 name=row['name'];parts=name.split('/');assert name not in names and all(p and p not in ('.','..') and all(c.isalnum() or c in '_.-' for c in p) for p in parts);names.add(name)
 size=row['bytes'];assert type(size) is int and 0<size<=67108864;total+=size;assert total<=134217728
 directory=destination
 for part in parts[:-1]:
  directory+='/'+part
  try:os.mkdir(directory,0o700)
  except FileExistsError:pass
  st=os.lstat(directory);assert stat.S_ISDIR(st.st_mode) and st.st_uid==os.geteuid() and stat.S_IMODE(st.st_mode)==0o700
 fd=os.open(destination+'/'+name,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o400);left=size;h=hashlib.sha256()
 try:
  while left:
   b=exact(min(left,1048576));h.update(b);view=memoryview(b)
   while view:written=os.write(fd,view);assert written>0;view=view[written:]
   left-=len(b)
  os.fsync(fd)
 finally:os.close(fd)
 assert h.hexdigest()==row['sha256']
assert not sys.stdin.buffer.read(1)
for folder,dirs,files in os.walk(destination,topdown=False):
 fd=os.open(folder,os.O_RDONLY|os.O_DIRECTORY);os.fsync(fd);os.close(fd)
print(json.dumps(dict(sealed=True,bytes=total,files=len(rows),inbox=destination)))
'''

class Board:
 def __init__(self,host,config,socket,out):
  if not re.fullmatch('[A-Za-z0-9_.@:-]{1,128}',host) or socket!='/run/vxgpud/control.sock':raise ValueError('explicit board alias and joint fixed socket required')
  self.prefix=['ssh']+(['-F',str(config)] if config else [])+['-o','BatchMode=yes','-o','ConnectTimeout=10',host];self.socket=socket;self.out=pathlib.Path(out);self.serial=0
 def command(self,argv,data=None,timeout=30):
  self.serial+=1;label='%04d'%self.serial;begin=time.monotonic();cmd=self.prefix+[' '.join(shlex.quote(str(a)) for a in argv)]
  (self.out/(label+'-intent.json')).write_bytes(wire(dict(argv=cmd,operation_started=True)))
  try:p=subprocess.run(cmd,input=data,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=timeout)
  except subprocess.TimeoutExpired as e:
   stdout=e.stdout or b'';stderr=e.stderr or b'';(self.out/(label+'-stdout.bin')).write_bytes(stdout);(self.out/(label+'-stderr.bin')).write_bytes(stderr);(self.out/(label+'-receipt.json')).write_bytes(wire(dict(argv=cmd,natural_exit=None,elapsed_s=time.monotonic()-begin,timeout=True,remote_outcome='UNKNOWN',stdout_bytes=len(stdout),stdout_sha256=digest(stdout),stderr_sha256=digest(stderr))));raise
  (self.out/(label+'-stdout.bin')).write_bytes(p.stdout);(self.out/(label+'-stderr.bin')).write_bytes(p.stderr);(self.out/(label+'-receipt.json')).write_bytes(wire(dict(argv=cmd,natural_exit=p.returncode,elapsed_s=time.monotonic()-begin,stdout_bytes=len(p.stdout),stdout_sha256=digest(p.stdout),stderr_sha256=digest(p.stderr))))
  if p.returncode not in (0,1):raise RuntimeError('SSH/transport exit %d; original request identity retained; query status, do not resubmit'%p.returncode)
  return p.returncode,p.stdout
 def rpc(self,command,producer=None,**values):
  # The native fixed-size file import runs synchronously. Give only this
  # registered CPU preparation a bounded30s client observation window.
  slow=command=='export' and values.get('destination_id','').startswith('mosaic-to-p8-v1:')
  argv=['/usr/bin/vxgpuctl','--socket',self.socket]+(['--timeout-ms','30000'] if slow else [])+[command]
  for k,v in dict(producer or {},**values).items():argv+=['--'+k.replace('_','-'),str(v)]
  code,raw=self.command(argv,timeout=40 if slow else 30);(self.out/('%04d-rpc.json'%self.serial)).write_bytes(raw);row=json.loads(raw)
  if type(row.get('ok')) is not bool or row.get('version')!=1:raise ValueError('actual RPC envelope required')
  if producer and row.get('boot_id')!=producer['boot_id']:raise ValueError('board restarted or boot identity changed')
  if not row['ok']:raise ServiceError(row['error'])
  if code:raise ValueError('RPC success/exit mismatch')
  return row['result']
 def fetch(self,export,row,name,directory):
  root=export.get('file_root');relative=row['path']
  if root!='/run/vxgpud/outputs' or not re.fullmatch('[A-Za-z0-9_-]{1,128}/'+re.escape(name),relative) or not 0<row['bytes']<=64*1024**2:raise ValueError('fixed original exporter artifact required')
  code,raw=self.command(['cat','--',root+'/'+relative],timeout=60)
  if code or len(raw)!=row['bytes'] or digest(raw)!=row['sha256']:raise ValueError('actual exported file extent/digest drift')
  p=pathlib.Path(directory)/name
  with p.open('xb') as f:f.write(raw);f.flush();os.fsync(f.fileno())
  p.chmod(0o400);return p
 def upload(self,receipt,package,inbox):
  leaf=receipt['source_result_sha256']
  if inbox!='/run/vxgpud/texture-inbox/'+leaf:raise ValueError('fixed original manifest-keyed inbox required')
  rows=[dict(row,name=name) for name,row in sorted(receipt['files'].items(),key=lambda item:(item[0]=='manifest.json',item[0]))];header=wire(rows);body=bytearray(struct.pack('!I',len(header))+header)
  for row in rows:
   raw=(pathlib.Path(package)/row['name']).read_bytes()
   if len(raw)!=row['bytes'] or digest(raw)!=row['sha256']:raise ValueError('sealed transfer input differs from conversion receipt')
   body.extend(raw)
  code,raw=self.command(['python3','-c',UPLOAD,leaf],bytes(body),timeout=120);proof=json.loads(raw)
  if code or proof.get('sealed') is not True or proof.get('inbox')!=inbox or proof.get('files')!=len(rows):raise ValueError('actual sealed transfer acknowledgement required')
  return proof

def poll(call,predicate,timeout,label):
 """Bounded read-only observations only; never retry a submit or accept here."""
 deadline=time.monotonic()+timeout
 while True:
  try:
   value=call()
   if predicate(value):return value
  except ServiceError as e:
   if e.code!=11:raise
  if time.monotonic()>=deadline:raise TimeoutError(label+' deadline; retain original request; no automatic resubmit')
  time.sleep(.25)
def mode(board,boot,target,timeout):
 board.rpc('mode',dict(boot_id=boot),mode=target)
 return poll(lambda:board.rpc('mode-status',dict(boot_id=boot)),lambda x:x.get('current')==target and x.get('switching') is False,timeout,'complete-request mode handoff')
def classify(meta,applied):
 physical=meta.get('scope')=='REAL_BOARD' and meta.get('cycle_valid') is True and type(meta.get('raw_cycles')) is int and meta['raw_cycles']>0 and applied.get('applied') is True and type(applied.get('gpu_raw_cycles')) is int and applied['gpu_raw_cycles']>0 and applied.get('clock_domain')=='LINUX_MONOTONIC'
 return dict(transport_complete=True,physical_execution_and_presentation=physical,numeric_class=meta['numeric_class'],neural_numeric_pass=physical and meta['numeric_class']=='PASS',status='REAL_BOARD_EXECUTION_DISPLAY_COMPLETE_NUMERIC_RECORDED' if physical else 'HOST_OR_INCOMPLETE_PHYSICAL_EVIDENCE_NO_BOARD_PASS')
def run(board,out,producer,timeout=3600,reuse_existing=False,input_id='frozen-1280x960'):
 out=pathlib.Path(out);record=dict(schema='s8-joint-loop-v1',producer=producer,reuse_existing=reuse_existing,input_id=input_id,scope='CONTROL_EVIDENCE_PENDING',gpu_raw_cycles=None,sd='NOT_TOUCHED',pl='NOT_RECONFIGURED',steps=[])
 try:
  record['mode_nn']=mode(board,producer['boot_id'],'nn',timeout)
  # Persist the exact identity before the single submit, even if SSH fails.
  if not reuse_existing:
   (out/'mosaic-submit-intent.json').write_bytes(wire(dict(producer=producer,entry_id='mosaic',input_id=input_id,automatic_resubmit=False)))
   record['submit']=board.rpc('submit',producer,entry_id='mosaic',input_id=input_id,parameters='{}')
  else:
   existing=board.rpc('status',producer)
   if existing.get('status')!='COMPLETED':raise ValueError('selected existing Mosaic result is not complete; never resubmit')
  def state():
   v=board.rpc('status',producer)
   if v.get('status') in ('FAILED','CANCELLED'):raise RuntimeError('original Mosaic ended: '+str(v))
   return v
  record['mosaic_status']=poll(state,lambda v:v.get('status')=='COMPLETED',timeout,'full original Mosaic')
  record['mosaic_result']=board.rpc('result',producer)
  if record['mosaic_result'].get('execution_complete') is not True:raise ValueError('original full-request completion required')
  record['mosaic_display']=board.rpc('display',producer,artifact=0)
  saved=board.rpc('export',producer,artifact=0,destination_id='mosaic-file-v1');record['original_export']=saved
  if saved.get('provider_flags')!=4:raise ValueError('explicit S8 joint native export required')
  original=out/'original';original.mkdir(mode=0o700);source=board.fetch(saved,saved['manifest'],'mosaic-result.json',original);board.fetch(saved,saved['tensor'],'output.f32',original)
  meta=json.loads(source.read_bytes());receipt,package=convert(source,out/'conversion',producer)
  expected=verify_conversion(receipt,package,producer,saved['manifest']['sha256'],saved['tensor']['sha256']);record['conversion_receipt_sha256']=digest((out/'conversion/conversion-receipt.json').read_bytes());record['package_id']=expected
  begin=time.monotonic();record['upload']=board.upload(receipt,package,saved['texture_inbox']);record['upload_ns']=int((time.monotonic()-begin)*1e9)
  record['mode_p8']=mode(board,producer['boot_id'],'p8',timeout)
  # EBUSY/EAGAIN here mean no texture was accepted: original startup100 or an
  # already running whole frame must finish. Accepted producer is idempotent.
  deadline=time.monotonic()+timeout
  while True:
   try:record['texture_accept']=board.rpc('export',producer,artifact=0,destination_id='mosaic-to-p8-v1:'+expected);break
   except ServiceError as e:
    if e.code not in (11,16) or time.monotonic()>=deadline:raise
    time.sleep(.25)
  def texture():
   value=board.rpc('export',producer,artifact=0,destination_id='mosaic-texture-status-v1')
   if value.get('phase') in ('FAILED','CANCELLED','SUPERSEDED'):raise RuntimeError('texture did not apply: '+str(value))
   return value
  applied=poll(texture,lambda v:v.get('phase')=='APPLIED' and v.get('applied') is True,timeout,'actual complete P8 texture frame and DP receipt');record['texture_applied']=applied
  if applied.get('source_result_sha256')!=saved['manifest']['sha256'] or applied.get('source_tensor_sha256')!=saved['tensor']['sha256'] or applied.get('package_id')!=expected:raise ValueError('final actual source-to-displayed-frame identity differs')
  record.update(classify(meta,applied));record['natural_exit']=0
 except BaseException as e:record.update(natural_exit=1,status='INCOMPLETE_RETAIN_ORIGINAL_REQUEST_NO_RESUBMIT',error=repr(e));raise
 finally:
  path=out/'loop-receipt.json'
  with path.open('xb') as f:f.write(wire(record));f.flush();os.fsync(f.fileno())
  path.chmod(0o400)
 return record
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--board',required=True);p.add_argument('--ssh-config');p.add_argument('--boot-id',required=True);p.add_argument('--client-id',required=True);p.add_argument('--request-id',required=True);p.add_argument('--out',required=True);p.add_argument('--timeout',type=int,default=3600);a=p.parse_args()
 for value in (a.boot_id,a.client_id,a.request_id):
  if not re.fullmatch('[A-Za-z0-9_.:-]{1,128}',value):p.error('explicit bounded original producer identity required')
 if not 1<=a.timeout<=86400:p.error('timeout1..86400 seconds')
 out=pathlib.Path(a.out);out.mkdir(mode=0o700,parents=True,exist_ok=False);board=Board(a.board,a.ssh_config,'/run/vxgpud/control.sock',out)
 record=run(board,out,dict(boot_id=a.boot_id,client_id=a.client_id,request_id=a.request_id),a.timeout);print(json.dumps(record,indent=2))
if __name__=='__main__':main()

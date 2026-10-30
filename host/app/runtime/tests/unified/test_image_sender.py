"""Unmodified PC sender over real UDP to typed shared-owner HOST fixture.
No GPU arithmetic, physical DP, board performance or preprocessing claim.
"""
import hashlib,json,pathlib,queue,socket,subprocess,sys,threading,time

def main():
 elf,outdir=sys.argv[1:];out=pathlib.Path(outdir);out.mkdir(parents=True,exist_ok=True)
 vendor=pathlib.Path('vendor/image-studio/src/vortex23-image-studio-720p-fast').resolve();sys.path.insert(0,str(vendor/'desktop'))
 import image_sender as sender
 from contract import PreparedImage,Rect,Params
 logs=[];lines=queue.Queue();p=subprocess.Popen([elf,'data/image/programs-v2/catalog.bin','data/image/programs-v2','vendor/image-studio/gpu_programs/tables-64KiB.bin','socket'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,universal_newlines=True)
 def collect():
  for line in p.stdout:logs.append(line);lines.put(line)
 worker=threading.Thread(target=collect);worker.start();client=None;port=None;rec=dict(scope='ORIGINAL_PC_SENDER_REAL_UDP_TYPED_HOST_OWNER_NOT_GPU_OR_DP',gpu_raw_cycles=None,client_lifecycle='FROZEN_GUI_ONE_CLIENT_PER_ACTION',source_sender_sha256=hashlib.sha256((vendor/'desktop/image_sender.py').read_bytes()).hexdigest(),transactions=[])
 try:
  deadline=time.monotonic()+20
  while time.monotonic()<deadline:
   line=lines.get(timeout=max(.1,deadline-time.monotonic()))
   if line.startswith('PORT '):port=int(line.split()[1]);break
  assert port and port!=0
  client=sender.Client('127.0.0.1',port,timeout=40,window=8)
  rgba=bytes((7,16,30,255))*(1280*720);prepared=PreparedImage(rgba,Rect(0,0,1280,720),hashlib.sha256(rgba).hexdigest())
  first=client.send_image(prepared,Params(1,1));rec['transactions'].append(first)
  assert first['metrics']['pass_count']==5 and first['metrics']['cache_hit']==0 and first['upload']['window']==8 and first['upload']['max_inflight']==8 and first['upload']['chunks']==3072 and first['upload']['retransmissions']>=3
  for params,expected in [(Params(1,1),0),(Params(0),0),(Params(4,2,4),2)]:
   client.close();client=sender.Client('127.0.0.1',port,timeout=40,window=8)
   item=client.select_style(first['input_id'],first['input_crc'],params);rec['transactions'].append(item);assert item['metrics']['pass_count']==expected
  assert rec['transactions'][1]['metrics']['cache_hit']==1
  assert all(x['metrics']['gpu_cycles']==0 and x['metrics']['timer_hz']==1000000000 for x in rec['transactions'])
  client.sock.sendto(b'STOP',('127.0.0.1',port));rc=p.wait(timeout=10);worker.join(timeout=2);assert rc==0 and not worker.is_alive();assert 'U15_ORIGINAL_PC_SENDER_SOCKET_TYPED_RUNTIME_PASS_HOST_MARKER_NOT_GPU_DP' in ''.join(logs)
  rec['natural_exit']=0;print('U15_ORIGINAL_UNMODIFIED_SENDER_WINDOW8_RETRANSMIT_STYLE_CACHE_PASS_HOST_NOT_GPU_DP gpu_raw_cycles=null')
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:
  if client:
   if p.poll() is None:client.sock.sendto(b'STOP',('127.0.0.1',port))
   client.close()
  if p.poll() is None:
   try:p.wait(timeout=10)
   except subprocess.TimeoutExpired:p.terminate();p.wait(timeout=5);rec['bounded_host_fixture_terminated']=True
  worker.join(timeout=3);rec['server_natural_exit']=p.returncode;(out/'server.log').write_text(''.join(logs));(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()

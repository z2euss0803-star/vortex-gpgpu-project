"""Frozen receiver protocol regression on real compiled C, not GPU/DP."""
import hashlib,json,os,pathlib,subprocess,sys,time

def main():
 name=sys.argv[1];out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False);rec=dict(scope='FROZEN_P8_NATIVE_C_RECEIVER_PROTOCOL_ONLY_NOT_SERVICE_OR_GPU',gpu_raw_cycles=None,commands=[])
 def run(argv):
  begin=time.monotonic();p=subprocess.run(list(map(str,argv)),stdout=subprocess.PIPE,stderr=subprocess.STDOUT);log=out/('command-%d.log'%len(rec['commands']));log.write_bytes(p.stdout);rec['commands'].append(dict(argv=list(map(str,argv)),natural_exit=p.returncode,elapsed_s=time.monotonic()-begin,sha256=hashlib.sha256(p.stdout).hexdigest(),log=str(log)));print(p.stdout.decode('utf8','replace')[-4000:],flush=True);assert p.returncode==0;return p.stdout
 try:
  specs=json.loads(pathlib.Path('docs/unified/U17-inputs.json').read_bytes())
  for row in specs['files']:raw=pathlib.Path(row['candidate']).read_bytes();assert len(raw)==row['bytes'] and hashlib.sha256(raw).hexdigest()==row['sha256']
  target=out/'libp8_texture_rx.so';run(['gcc','-std=c11','-O2','-g1','-Wall','-Wextra','-Werror','-shared','-fPIC','vendor/p8/network/texture_rx.c','-o',target]);text=run([sys.executable,'vendor/p8/tests/texture_rx_protocol.py','--dll',target.resolve(),'-v']);assert b'OK' in text and b'skipped' not in text;rec.update(natural_exit=0,library_sha256=hashlib.sha256(target.read_bytes()).hexdigest())
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:(out/'receipt.json').write_bytes((json.dumps(rec,indent=2)+'\n').encode())
if __name__=='__main__':main()

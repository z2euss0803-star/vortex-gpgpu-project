from scripts.unified.hunet_program import selected_directory as selected_hunet_directory
"""Public installed NN RPC->original HUNet page, HOST marker not inference/DP."""
import pathlib,subprocess,sys,time,json,os,tempfile,shutil,stat
from scripts.unified.check_u11 import sha,artifact
from scripts.unified.prepare_hunet_view import prepare
from scripts.unified.capture_native_private import capture
def main():
 name=sys.argv[1];out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False)
 record=dict(scope='INSTALLED_HUNET_PUBLIC_RPC_HOST_PROTOCOL_MARKER_NOT_NN_INFERENCE_PHYSICAL_DP',commands=[],gpu_raw_cycles=None)
 def run(argv):
  argv=list(map(str,argv));log=out/('command-%02d.log'%len(record['commands']));begin=time.monotonic()
  with log.open('xb') as f:r=subprocess.run(argv,stdout=f,stderr=subprocess.STDOUT,timeout=None if argv[0]=='make' else 180)
  text=log.read_text(errors='replace');record['commands'].append(dict(argv=argv,natural_exit=r.returncode,elapsed_s=time.monotonic()-begin,log=str(log),sha256=sha(log)));record.setdefault('private_evidence',[]).extend(capture(text,out,len(record['commands'])));print(text[-2400:],flush=True);return r.returncode,text
 try:
  assets=pathlib.Path(tempfile.mkdtemp(prefix='vx-native-view-assets-'))/'view';record['view_package']=prepare('data/demo',assets);snapshot=out/'view';snapshot.mkdir(mode=0o700)
  for original in assets.iterdir():
   p=snapshot/original.name;shutil.copyfile(str(original),str(p));p.chmod(stat.S_IMODE(original.stat().st_mode));assert sha(original)==sha(p)
  weights=pathlib.Path('/home/z2euss/workspace/unified_gpu_runtime_20261021/inputs/hunet-674069f4.bin');assert sha(weights)=='674069f415d51ce502dd00e579125c0ca8fe6d7e71a71d08cfae457e293e39bc'
  package=out/'hunet-model';code,text=run([sys.executable,'-m','scripts.unified.generate_hunet_package',weights,package]);assert code==0;record['model']=json.loads(text)
  for p in package.iterdir():p.chmod(0o400)
  package.chmod(0o500)
  jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2)
  targets=['test_native_daemon_hunet','test_native_result_view','test_installed_media','test_daemon','vxgpud','hunet-owner-catalog.bin','catalog-fixture.bin']
  code,_=run(['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1']+[out/p for p in targets]+['-j'+str(jobs)]);assert code==0
  code,_=run([out/'test_installed_media']);assert code==0
  code,_=run([out/'test_daemon',out/'catalog-fixture.bin',out/'startup-error-journal','startup-journal']);assert code==0
  code,_=run([out/'test_native_result_view',assets,record['view_package']['sha256']]);assert code==0
  code,text=run([out/'test_native_daemon_hunet',package,out/'hunet-owner-catalog.bin',selected_hunet_directory('.')/'code.bin','unused',out/'hunet-view','view',assets,record['view_package']['sha256']]);assert code==0 and 'U20_INSTALLED_NN_RPC_NATIVE_VIEW_AND_RETIREMENT_PASS_HOST_MARKER_NOT_INFERENCE_GPU_DP' in text
  record['natural_exit']=0
 except BaseException as e:record.update(natural_exit=1,error=repr(e));raise
 finally:
  record['artifacts']=[artifact(p) for p in out.rglob('*') if p.is_file() and p.name!='receipt.json'];(out/'receipt.json').write_text(json.dumps(record,indent=2)+'\n')
if __name__=='__main__':main()

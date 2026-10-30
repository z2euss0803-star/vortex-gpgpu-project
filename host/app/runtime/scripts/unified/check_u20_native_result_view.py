"""Original HUNet page from retained reference bytes; no GPU or physical DP."""
import pathlib,subprocess,sys,json,time,os,tempfile,shutil,stat
from scripts.unified.check_u11 import sha,artifact
from scripts.unified.capture_native_private import capture
from scripts.unified.prepare_hunet_view import prepare
def main():
 name,mode=sys.argv[1:];assert mode in ('red','normal')
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False)
 record=dict(scope='NATIVE_RESULT_VIEW_HOST_REFERENCE_ONLY_NOT_GPU_INFERENCE_PHYSICAL_DP',commands=[],gpu_raw_cycles=None)
 def run(argv):
  argv=list(map(str,argv));log=out/('command-%03d.log'%len(record['commands']));begin=time.monotonic()
  with log.open('xb') as f:r=subprocess.run(argv,stdout=f,stderr=subprocess.STDOUT,timeout=None if argv[0]=='make' else 60)
  text=log.read_text(errors='replace');record['commands'].append(dict(argv=argv,natural_exit=r.returncode,elapsed_s=time.monotonic()-begin,log=str(log),sha256=sha(log)));record.setdefault('private_evidence',[]).extend(capture(text,out,len(record['commands'])));print(text[-2000:],flush=True);(out/'receipt.json').write_text(json.dumps(record,indent=2)+'\n');return r.returncode,text
 try:
  # Keep shared build directories unchanged. Installed inputs live below a
  # fresh private0700 parent, with a hash-checked snapshot inside this run.
  asset_owner=pathlib.Path(tempfile.mkdtemp(prefix='vx-native-view-assets-'))
  record['view_package']=prepare('data/demo',asset_owner/'view')
  snapshot=out/'view';snapshot.mkdir(mode=0o700);record['asset_snapshot']=[]
  for original in sorted((asset_owner/'view').iterdir()):
   target=snapshot/original.name;shutil.copyfile(str(original),str(target));target.chmod(stat.S_IMODE(original.stat().st_mode));assert sha(original)==sha(target)
   record['asset_snapshot'].append(artifact(target))
  jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2)
  code,_=run(['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1',out/'test_native_result_view','-j'+str(jobs)]);assert code==0,'compile is not functional RED'
  code,text=run([out/'test_native_result_view',asset_owner/'view',record['view_package']['sha256']])
  if mode=='red':assert code==1 and 'U20 native HUNet result must bind sealed original source/labels/glyphs in shared CPU budget' in text;record['functional_red']='NATIVE_RESULT_HUNET_VIEW_NOT_BOUND'
  else:assert code==0 and 'U20_NATIVE_HUNET_SOURCE_TOP5_ROI_ORIGINAL_PIXELS_SHARED_BUDGET_PASS_HOST_REFERENCE_ONLY' in text
  record['natural_exit']=0
 except BaseException as e:record.update(natural_exit=1,error=repr(e));raise
 finally:
  record['artifacts']=[artifact(p) for p in out.rglob('*') if p.is_file() and p.name!='receipt.json'];(out/'receipt.json').write_text(json.dumps(record,indent=2)+'\n')
if __name__=='__main__':main()

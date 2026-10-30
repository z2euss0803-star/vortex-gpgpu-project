"""Actual common-owner P8 scene component; explicit HOST C oracle, not GPU."""
import pathlib,subprocess,sys,json,time,os,re,stat,shutil
from scripts.unified.check_u11 import artifact,sha
def main():
 name,mode=sys.argv[1:];assert mode in ('red','frame-red','normal','sanitize','thread')
 out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False)
 rec=dict(scope='NATIVE_P8_SCENE_SERVICE_HOST_CONTROL_NOT_GPU_DP',gpu_raw_cycles=None,commands=[],artifacts=[])
 def run(argv,expected=0):
  argv=list(map(str,argv));log=out/('command-%03d.log'%len(rec['commands']));begin=time.monotonic()
  # HOST CPU reference/controller bound; original GPU and wire bounds unchanged.
  with log.open('xb') as f:r=subprocess.run(argv,stdout=f,stderr=subprocess.STDOUT,timeout=None if argv[0]=='make' else 600)
  rec['commands'].append(dict(argv=argv,natural_exit=r.returncode,elapsed_s=time.monotonic()-begin,log=str(log),sha256=sha(log)))
  (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n');text=log.read_text(errors='replace');print(text[-2400:],flush=True)
  for owner in re.findall(r'^NATIVE_DAEMON_OWNER=(/tmp/vx-native-p8-daemon-[A-Za-z0-9]{6})$',text,re.M):
   source=pathlib.Path(owner);state=source.lstat();assert stat.S_ISDIR(state.st_mode) and state.st_uid==os.geteuid() and stat.S_IMODE(state.st_mode)==0o700;dest=out/('private-%03d'%len(rec['commands']));dest.mkdir(mode=0o700);members=[]
   for folder,dirs,files in os.walk(str(source),followlinks=False):
    for name in sorted(dirs+files):
     path=pathlib.Path(folder)/name;s=path.lstat();assert s.st_uid==os.geteuid();target=dest/path.relative_to(source);row=dict(path=str(path.relative_to(source)),mode=stat.S_IMODE(s.st_mode))
     if stat.S_ISDIR(s.st_mode):target.mkdir(mode=stat.S_IMODE(s.st_mode));row['kind']='directory'
     elif stat.S_ISREG(s.st_mode):
      shutil.copyfile(str(path),str(target));assert sha(path)==sha(target);after=path.lstat();assert (s.st_ino,s.st_size,s.st_mtime_ns)==(after.st_ino,after.st_size,after.st_mtime_ns);target.chmod(stat.S_IMODE(s.st_mode));row.update(kind='file',bytes=s.st_size,sha256=sha(target))
     elif stat.S_ISLNK(s.st_mode):row.update(kind='symlink_metadata_only',target=os.readlink(str(path)))
     else:row['kind']='nonfile_metadata_only'
     members.append(row)
   rec.setdefault('native_private_evidence',[]).append(dict(owner=owner,copy=str(dest),members=members))
  (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
  assert r.returncode==expected and not any(x in text for x in ('ERROR: AddressSanitizer','WARNING: ThreadSanitizer','runtime error:'))
  return text
 try:
  cmd=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1'];jobs=int(os.environ.get('VX_UNIFIED_JOBS','1'));assert jobs in (1,2)
  if mode in ('sanitize','thread'):cmd+=['SANITIZE='+('address,undefined' if mode=='sanitize' else 'thread')]
  elf=out/'test_native_p8_service';extra=[] if mode in ('red','frame-red') else [out/'test_native_p8_daemon'];run(cmd+[elf]+extra+['-j'+str(jobs)])
  text=run([elf,'data/p8/programs/catalog.bin','data/p8/programs','unused',out/'scene','scene-service'],1 if mode in ('red','frame-red') else 0)
  assert ('U20 native frame requires explicit zero-wire timing evidence' if mode=='frame-red' else 'U20 native P8 scene service must use common worker' if mode=='red' else 'U20_NATIVE_P8_SCENE_ORIGINAL_FIRST_FRAME_SAME_WORKER_HOST_NOT_GPU_DP') in text
  if extra:
   for case in ('normal','partial','fault'):
    run([extra[0],'data/p8/programs/catalog.bin','data/p8/programs','data/p8',out/('daemon-'+case),case])
   run([sys.executable,'-m','unittest','discover','-s','tests/unified','-p','test_extension_audit.py','-v'])
  rec['natural_exit']=0
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:
  rec['artifacts']=[artifact(p) for p in out.rglob('*') if p.is_file() and p.name!='receipt.json']
  (out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()

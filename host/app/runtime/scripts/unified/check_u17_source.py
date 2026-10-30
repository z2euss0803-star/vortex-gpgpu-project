"""Real imported-file contract tests; no GPU or physical display evidence."""
import json,os,pathlib,subprocess,sys,time,hashlib

def main():
 name,mode=sys.argv[1:];assert mode in ('red','normal','sanitize','thread');out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False);rec=dict(scope='MOSAIC_FILE_SOURCE_IMPORT_ONLY_NOT_TEXTURE_RENDER_OR_GPU',mode=mode,gpu_raw_cycles=None,commands=[])
 def run(argv,expected=0):
  argv=list(map(str,argv));begin=time.monotonic();p=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);log=out/('command-%d.log'%len(rec['commands']));log.write_bytes(p.stdout);rec['commands'].append(dict(argv=argv,natural_exit=p.returncode,elapsed_s=time.monotonic()-begin,log=str(log),sha256=hashlib.sha256(p.stdout).hexdigest()));print(p.stdout.decode('utf8','replace')[-3000:],flush=True);assert p.returncode==expected;assert not any(x in p.stdout for x in (b'ERROR: AddressSanitizer',b'WARNING: ThreadSanitizer',b'runtime error:'));return p.stdout
 try:
  cmd=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1'];cmd+=['SANITIZE='+('thread' if mode=='thread' else 'address,undefined')] if mode in ('sanitize','thread') else [];elf=out/'test_texture_source';run(cmd+[elf,'-j'+os.environ.get('VX_UNIFIED_JOBS','1')]);text=run([elf],1 if mode=='red' else 0)
  assert (b'U17_TEXTURE_ASSERT complete Mosaic file must import' if mode=='red' else b'U17_MOSAIC_SOURCE_IMPORT_FINITE_HASH_SCOPE_AND_RGB_PASS_HOST_NOT_GPU_DP') in text;rec['natural_exit']=0
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:(out/'receipt.json').write_bytes((json.dumps(rec,indent=2)+'\n').encode())
if __name__=='__main__':main()

import hashlib,importlib.util,json,os,pathlib,subprocess,sys,time,struct,re
from scripts.unified.texture_pillow import dependency
from scripts.unified.texture_package import build_package

def main():
 name=sys.argv[1];mode='normal';out=pathlib.Path('build')/name;out.mkdir(parents=True,exist_ok=False);rec=dict(scope='U17_EXACT_NAMED_PLAN_TARGETS_NOT_GPU_DP',gpu_raw_cycles=None,commands=[])
 def run(argv,expected=0):
  begin=time.monotonic();p=subprocess.run(list(map(str,argv)),stdout=subprocess.PIPE,stderr=subprocess.STDOUT);log=out/('command-%d.log'%len(rec['commands']));log.write_bytes(p.stdout);rec['commands'].append(dict(argv=list(map(str,argv)),natural_exit=p.returncode,elapsed_s=time.monotonic()-begin,log=str(log),sha256=hashlib.sha256(p.stdout).hexdigest()));print(p.stdout.decode('utf8','replace')[-3000:],flush=True);assert p.returncode==expected;return p.stdout
 try:
  rec['dependency']=dependency();spec=importlib.util.spec_from_file_location('texture_fixtures','tests/unified/test_texture_package.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);module.Package().fixture(out/'input');package=build_package(out/'input/result.json',out/'package');rec['package_id']=package['package_id'];cmd=['make','-f','Makefile.unified','OUT='+str(out),'DEBUG_FLAGS=-g1','TEXTURE_PACKAGE='+str(out/'package'),'TEXTURE_ID='+package['package_id']];run(cmd+[out/'test_texture_source',out/'test_p8_udp','-j'+os.environ.get('VX_UNIFIED_JOBS','1')]);text=run(cmd+['test-unified-texture','test-unified-p8-udp','-j1']);assert b'U17_MOSAIC_SOURCE_IMPORT_FINITE_HASH_SCOPE_AND_RGB_PASS_HOST_NOT_GPU_DP' in text and b'U17_ORIGINAL_C_UDP_FULL_DUAL_LOD_PIN_CANCEL_SCOPE_AND_FILE_STAGE_PASS_HOST_NOT_GPU_DP' in text;rec['natural_exit']=0
 except BaseException as e:rec.update(natural_exit=1,error=repr(e));raise
 finally:(out/'receipt.json').write_bytes((json.dumps(rec,indent=2)+'\n').encode())
if __name__=='__main__':main()

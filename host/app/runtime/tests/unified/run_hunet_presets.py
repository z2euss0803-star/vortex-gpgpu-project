"""Focused actual native C++ tests. No GPU, B, installation or source mutation."""
import argparse,hashlib,json,pathlib,subprocess,time,os,shlex,shutil,tempfile
ROOT=pathlib.Path(__file__).resolve().parents[2]
def main():
 p=argparse.ArgumentParser();p.add_argument('--out',required=True,type=pathlib.Path);p.add_argument('--red',action='store_true');p.add_argument('--native-model',type=pathlib.Path);p.add_argument('--native-red',action='store_true');a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
 rec=dict(scope='R5_HUNET_PRESETS_FOCUSED_HOST_NOT_GPU',commands=[],sources={},started_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()))
 def run(name,cmd,expected=0):
  result=subprocess.run([str(v) for v in cmd],cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(out/(name+'.log')).write_bytes(result.stdout);rec['commands'].append(dict(name=name,argv=[str(v) for v in cmd],natural_exit=result.returncode,expected_exit=expected));(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n');print(name,result.returncode,result.stdout.decode(errors='replace')[-8000:],flush=True);assert result.returncode==expected,name
 for rel in ('tests/unified/test_hunet_presets_installation.cpp','tests/unified/test_hunet_presets_native.cpp','tests/unified/run_hunet_presets.py','include/vxunified/native_nn.hpp','unified/service/native_nn.cpp','unified/service/installation.cpp','include/vxnn/request_input.hpp'):
  rec['sources'][rel]=hashlib.sha256((ROOT/rel).read_bytes()).hexdigest()
 json_root=pathlib.Path('/var/tmp/s8_300s_optimization_20261026/deps/json-c/extracted/usr');json_library=json_root/'lib/x86_64-linux-gnu/libjson-c.a';rec['json_dependency_sha256']=hashlib.sha256(json_library.read_bytes()).hexdigest()
 flags=['-std=c++17','-O2','-g1','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-ffunction-sections','-fdata-sections','-Iinclude','-I.','-I'+str(json_root/'include')]
 run('hash-compile',['gcc','-O2','-Iinclude','-c','core/hunet_sha256.c','-o',out/'sha.o'])
 run('installation-build',['g++']+flags+['tests/unified/test_hunet_presets_installation.cpp','unified/service/installation.cpp','nnrt/package/canonical.cpp',out/'sha.o','-Wl,--gc-sections',json_library,'-lm','-pthread','-o',out/'installation'])
 run('installation-test',[out/'installation'],1 if a.red else 0)
 if a.red:assert 'sealed optional HUNet presets must decode' in (out/'installation-test.log').read_text(),'RED must be the missing presets behavior, not a fixture failure'
 if a.native_model:
  assert os.geteuid()==0,'root-owned presets are tested using fresh root-owned /tmp fixtures'
  private=pathlib.Path(tempfile.mkdtemp(prefix='vx-hunet-presets-native-332-'));package=private/'package';shutil.copytree(a.native_model,package)
  rec['copied_package']=dict(source=str(a.native_model),copy=str(package),manifest_sha256=hashlib.sha256((package/'manifest.json').read_bytes()).hexdigest())
  build=out/'native-build';build.mkdir()
  run('catalog-native',['python3','-m','tests.nnrt.hunet_native_catalog_fixture',build/'native-catalog.bin'])
  run('catalog-owner',['python3','-m','tests.unified.hunet_owner_catalog_fixture',build/'native-catalog.bin',build/'owner-catalog.bin'])
  target=build/'test_native_nn_hunet'
  run('native-build',['make','-f','Makefile.unified','OUT='+str(build),'DEBUG_FLAGS=-g1','NN_JSON_FLAGS=-I'+str(json_root/'include'),'NN_JSON_LIBS='+str(json_library)+' -lm','-j1',target])
  command=next(shlex.split(line) for line in reversed((out/'native-build.log').read_text().splitlines()) if line.startswith('g++ ') and line.endswith('-o '+str(target)))
  run('native-presets-object',['g++']+flags+(['-DVX_PRESETS_RED_ONLY'] if a.native_red else [])+['-fno-fast-math','-ffp-contract=off','-c','tests/unified/test_hunet_presets_native.cpp','-o',build/'presets-test.o'])
  command=[str(build/'presets-test.o') if word.endswith('/tests/unified/test_native_nn_hunet.o') else str(build/'test_presets') if word==str(target) else word for word in command]
  run('native-presets-link',command)
  run('native-presets-test',[build/'test_presets',package,build/'owner-catalog.bin',ROOT/'data/nnrt/programs/hunet-fp16-v2/code.bin','normal'],1 if a.native_red else 0)
  if a.native_red:assert 'fixed128 entries must be listed outside dynamic8 registry' in (out/'native-presets-test.log').read_text(),'qualified native metadata RED'
 rec['status']='EXPECTED_PRESETS_INSTALLATION_RED' if a.red else 'FOCUSED_INSTALLATION_HOST_PASS_NOT_GPU';(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()

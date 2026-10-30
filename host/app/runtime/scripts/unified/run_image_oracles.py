import json,pathlib,subprocess,sys,time
from scripts.unified.verify_image_outputs import verify
out=pathlib.Path(sys.argv[1]);dest=out/'oracles';dest.mkdir()
commands=[]
def run(argv):
 begin=time.monotonic();p=subprocess.run([str(x) for x in argv],stdout=subprocess.PIPE,stderr=subprocess.STDOUT);path=dest/('command-%d.log'%len(commands));path.write_bytes(p.stdout);commands.append(dict(argv=[str(x) for x in argv],natural_exit=p.returncode,elapsed_s=time.monotonic()-begin,log=str(path)));(dest/'commands.json').write_text(json.dumps(commands,indent=2)+'\n');print(p.stdout.decode('utf8','replace'),flush=True);assert p.returncode==0
 return p.stdout.decode('utf8','replace')
result=verify(out/'test_image_oracles',dest,run);(dest/'receipt.json').write_text(json.dumps(result,indent=2)+'\n');print('U14_IMAGE_91_FROZEN_OUTPUTS_MATCH_HOST_NOT_GPU gpu_raw_cycles=null')

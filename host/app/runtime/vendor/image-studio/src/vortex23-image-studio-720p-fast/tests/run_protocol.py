from pathlib import Path
import subprocess,os
S=Path(__file__).resolve().parents[1];ROOT=S.parents[1];OUT=ROOT/'reports/v23-image-studio-720p-fast-20261016/protocol';OUT.mkdir(exist_ok=True)
assert (S/'network/image_rx.c').exists(),'image receiver not implemented'
cc=Path('E:/vivado2020.1/Vitis/2020.1/tps/win64/msys64/mingw64/bin/gcc.exe');env=os.environ.copy();env['PATH']=str(cc.parent)+os.pathsep+str(cc.parents[2]/'usr/bin')+os.pathsep+env['PATH']
for label,args in [('compile',[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-I'+str(S),'-I'+str(ROOT/'src/vortex23-r4-local/sw'),S/'network/image_protocol.c',S/'network/image_rx.c',S/'image_crc.c',S/'tests/test_image_rx.c','-o',OUT/'rx.exe']),('test',[OUT/'rx.exe'])]:
 with (OUT/(label+'.log')).open('wb') as f:r=subprocess.run([str(x) for x in args],env=env,stdout=f,stderr=subprocess.STDOUT,timeout=60)
 assert r.returncode==0,(label,(OUT/(label+'.log')).read_text(errors='replace')[-1500:])
print('IMAGE_PROTOCOL_OWNER_BOUNDS_PASS')

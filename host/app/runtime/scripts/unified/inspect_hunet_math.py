"""Read the frozen expf object; never patch the original toolchain library."""
import hashlib,json,pathlib,subprocess,sys
from scripts.unified.audit_int16_layout import elf_info
from scripts.s8.audit_rv32 import sections
def main():
 out=pathlib.Path('build')/sys.argv[1];out.mkdir(parents=True,exist_ok=False);env=json.loads(pathlib.Path('/home/z2euss/workspace/linux_nn_runtime_upgrade_20261021/env.json').read_text());tools=pathlib.Path(env['rv32_tools']);lib=tools/'libc32/lib/libc.a';ar=tools/'riscv32-gnu-toolchain/bin/riscv32-unknown-elf-ar';p=out/'expf.o';p.write_bytes(subprocess.check_output([str(ar),'p',str(lib),'expf.o']));logs={}
 for label,argv in [('sections',[tools/'llvm-vortex/bin/llvm-readelf','-S','-s','-r',p]),('disasm',[tools/'llvm-vortex/bin/llvm-objdump','-dr',p])]:
  proc=subprocess.run([str(x) for x in argv],stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(out/(label+'.log')).write_bytes(proc.stdout);logs[label]=dict(argv=[str(x) for x in argv],natural_exit=proc.returncode);print(proc.stdout.decode('utf8','replace'));assert proc.returncode==0
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();rec=dict(scope='FROZEN_LIBC_EXPF_STATIC_READONLY_TABLE_INVESTIGATION_NOT_GPU',libc_sha256=sha(lib),object_sha256=sha(p),commands=logs,natural_exit=0,gpu_raw_cycles=None);(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
if __name__=='__main__':main()

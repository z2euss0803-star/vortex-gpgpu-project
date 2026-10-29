"""Link the test to the BSP generated from THIS XSA without Eclipse app discovery."""
from pathlib import Path
import argparse, hashlib, json, re, subprocess, struct
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--workspace',type=Path,default=ROOT/'build/vitis_final')
args=parser.parse_args()
workspace=args.workspace.resolve()
bsp=workspace/'gpgpu_platform/psu_cortexa53_0/standalone_domain/bsp/psu_cortexa53_0'
out=ROOT/'build/artifacts'
out.mkdir(parents=True,exist_ok=True)
compiler=Path(r'G:\xilinx\Vitis\2020.1\gnu\aarch64\nt\aarch64-none\bin')
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
spr=json.loads((workspace/'gpgpu_platform/platform.spr').read_text())
xsa=Path(spr['platHandOff'])
assert xsa.parent==out, 'This build must use this project hardware export'
assert sha(xsa)==sha(workspace/'gpgpu_platform/hw'/xsa.name)
text=(bsp/'include/xparameters.h').read_text()
def macro(name):
    return int(re.search(r'^#define '+name+r' (0x[0-9A-Fa-f]+)',text,re.M).group(1),16)
assert macro('XPAR_HPM_BASEADDR')==0xa0000000
assert macro('XPAR_HPM_HIGHADDR')==0xa000ffff
# Reused layout places all load/stack/heap sections in lower DDR, excluding the
# final 1 MiB reserved by the PS tools. Verify that memory exists in the NEW BSP.
assert macro('XPAR_PSU_DDR_0_S_AXI_BASEADDR')==0
assert macro('XPAR_PSU_DDR_0_S_AXI_HIGHADDR')>=0x7fefffff
linker=ROOT/'sw/lscript.ld'
assert 'psu_ddr_0_MEM_0 : ORIGIN = 0x0, LENGTH = 0x10000000' in linker.read_text()
log=[]
def run(cmd):
    result=subprocess.run(list(map(str,cmd)),cwd=ROOT,capture_output=True,text=True,timeout=60)
    log.extend([subprocess.list2cmdline(list(map(str,cmd))),result.stdout,result.stderr])
    (ROOT/'evidence/build_elf.log').write_text('\n'.join(log),encoding='utf-8')
    if result.returncode: raise SystemExit(result.stderr)
    return result.stdout
obj=out/'main.o'; elf=out/'gpgpu_test.elf'
run([compiler/'aarch64-none-elf-gcc.exe','-Wall','-Wextra','-Werror','-O0','-g3',
     '-I'+str(bsp/'include'),'-I'+str(ROOT/'sw'),'-c',ROOT/'sw/main.c','-o',obj])
run([compiler/'aarch64-none-elf-gcc.exe','-Wl,-T',f'-Wl,{linker}',
     '-L'+str(bsp/'lib'),f'-Wl,-Map,{out / "gpgpu_test.map"}', '-o',elf,obj,
     '-Wl,--start-group','-lxil','-lgcc','-lc','-Wl,--end-group'])
headers=run([compiler/'aarch64-none-elf-readelf.exe','-h','-l',elf])
(ROOT/'evidence/elf_headers.txt').write_text(headers,encoding='utf-8')
data=elf.read_bytes()
assert data[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',data,18)[0]==183
phoff=struct.unpack_from('<Q',data,32)[0]
phsize,phnum=struct.unpack_from('<HH',data,54)
loads=[]
for n in range(phnum):
    off=phoff+n*phsize
    if struct.unpack_from('<I',data,off)[0]!=1:continue
    physical=struct.unpack_from('<Q',data,off+24)[0]
    size=struct.unpack_from('<Q',data,off+40)[0]
    assert physical+size<=0x10000000, 'A53 load segment overlaps GPU DDR'
    loads.append({'physical_address':hex(physical),'memory_size':size})
translation=bsp/'libsrc/standalone_v7_2/src/translation_table.S'
assert '0x8000_0000 - 0xBFFF_FFFF' in translation.read_text()
provenance={'xsa':str(xsa),'xsa_sha256':sha(xsa),'bsp_libxil_sha256':sha(bsp/'lib/libxil.a'),
            'xparameters_sha256':sha(bsp/'include/xparameters.h'),
            'translation_table_sha256':sha(translation),
            'main_sha256':sha(ROOT/'sw/main.c'),'gpgpu_hw_sha256':sha(ROOT/'sw/gpgpu_hw.h'),
            'linker_sha256':sha(linker),'elf_sha256':sha(elf),
            'kernel_sha256':sha(ROOT/'kernel/vecadd16.bin'),
            'kernel_header_sha256':sha(ROOT/'sw/vecadd16.h'),'load_segments':loads,
            'extended_sources':{f.name:sha(f) for f in (ROOT/'sw').glob('*.h')},
            'elf_machine':'AArch64','board_run':False,'workspace':str(workspace)}
(ROOT/'evidence/software_provenance.json').write_text(json.dumps(provenance,indent=2),encoding='utf-8')
log.append('A53_ELF_BUILD_PASS: matching new BSP, compile and link; NOT run on board')
(ROOT/'evidence/build_elf.log').write_text('\n'.join(log),encoding='utf-8')
print(log[-1]); print(elf)

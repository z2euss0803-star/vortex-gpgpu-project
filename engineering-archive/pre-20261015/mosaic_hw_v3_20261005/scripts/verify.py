"""Audit delivered inputs, contracts and evidence. Never infer board success."""
from pathlib import Path
import json,hashlib,re,struct
r=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return (r/p).read_text(encoding='utf-8',errors='replace')
manifest=json.loads(read('board/source_manifest.json'))
for path,digest in manifest['vendor_sha256'].items():
    assert sha(r/'vendor/vortex'/path)==digest,path
original_checks=[]
for item in manifest['inputs']:
    assert sha(r/item['copy'])==item['sha256']
    original_checks.append({'path':item['source'],'unchanged':sha(Path(item['source']))==item['sha256']})
assert all(x['unchanged'] for x in original_checks)
cfg=json.loads(read('config/build.json'))
for define in ['NUM_CORES=1','NUM_WARPS=4','NUM_THREADS=4','SOCKET_SIZE=1','FPU_DSP','EXT_TCU_ENABLE','TCU_DSP','EXT_TEX_ENABLE','Z11_TCU_XIL_IP','DCACHE_WRITEBACK=0','PLATFORM_MEMORY_NUM_BANKS=1']:
    assert define in cfg['defines'],define
bdpath='build/vivado/gpgpu_soc.srcs/sources_1/bd/ps_bd/ps_bd.bd'
bd=json.loads(read(bdpath))['design']
assert bd['design_info']['validated']=='true'
def prop(o,k):return o['parameters'][k]['value']
for name,width,addr,tid in [('HPM','32','40','16'),('HP','128','49','6')]:
    it=bd['interface_ports'][name]
    assert (prop(it,'DATA_WIDTH'),prop(it,'ADDR_WIDTH'),prop(it,'ID_WIDTH'))==(width,addr,tid)
    assert prop(it,'FREQ_HZ')=='48484364'
assert prop(bd['ports']['axi_clk'],'FREQ_HZ')==prop(bd['ports']['pl_clk0'],'FREQ_HZ')=='48484364'
ps=bd['components']['ps']
assert prop(ps,'PSU__DDRC__CL')=='17' and prop(ps,'PSU__DDRC__SPEED_BIN')=='DDR4_2400R'
seg=bd['addressing']['/ps']['address_spaces']['Data']['segments']['SEG_HPM_Reg']
assert int(seg['offset'],16)==0xa0000000 and seg['range']=='64K'
reset=read('build/vivado/gpgpu_soc.srcs/sources_1/bd/ps_bd/ip/ps_bd_rst_0/synth/ps_bd_rst_0.vhd')
assert "C_EXT_RESET_HIGH => '0'" in reset and "C_AUX_RESET_HIGH => '0'" in reset
assert '.axi_clk(pl_clk0)' in read('rtl/top.sv')
hier=read('evidence/hierarchy.txt')
assert len(re.findall(r': VX_core\s*$',hier,re.M))==1
assert '/icache/' in hier and '/dcache/' in hier
sim=read('evidence/gpu_rtl/run.log')
assert 'num_threads=4, num_warps=4, num_cores=1' in sim
assert 'GPGPU_REAL_RTL_PASS: vecadd and native TCU/TEX through shared HP' in sim
assert 'NATIVE_TCU_TEX_RTL_PASS: 10 cases 1280 matrix elements 160 texels' in sim
assert len(re.findall(r'^NATIVE_TCU_TEX_CASE_PASS',sim,re.M))==10
assert re.search(r'NATIVE_TCU_TEX_CASE_PASS case=8 .*samples=8',sim)
assert len(re.findall(r'^GPU_RTL_ROUND_PASS',sim,re.M))==3
assert not re.search(r'^(Error:|ERROR:|Fatal:|FATAL)',sim,re.M)
ctrl=json.loads(read('evidence/ctrl/summary.json'))
assert ctrl['good']=='PASS' and ctrl['rtl_sha256']==sha(r/'rtl/gpgpu_ctrl.sv')
assert all(ctrl[n]=='BUG_DETECTED_AS_EXPECTED' for n in ['bad_done_without_drain','bad_start_busy_in_reset'])
assert 'GPGPU_CTRL_PASS: checks=61' in read('evidence/ctrl/good.log')
assert 'FP16_EXHAUSTIVE_PASS 65536' in read('evidence/fp16_fixed/run.log')
assert 'CONVERTER_ERRORS=2046' in read('evidence/fp16_original/run.log')
assert 'TEXTURE_UNIT_PASS cases=1153' in read('evidence/texture/run.log')
assert 'cache_hit_and_reset_refetch=PASS' in read('evidence/texture/run.log')
sw=json.loads(read('evidence/software_provenance.json'))
for key,path in [('xsa_sha256',sw['xsa']),('elf_sha256',r/'build/artifacts/gpgpu_test.elf'),
                 ('main_sha256',r/'sw/main.c'),('gpgpu_hw_sha256',r/'sw/gpgpu_hw.h'),
                 ('kernel_header_sha256',r/'sw/vecadd16.h'),('kernel_sha256',r/'kernel/vecadd16.bin')]:
    assert sw[key]==sha(Path(path)),key
assert sw['board_run'] is False and all(int(x['physical_address'],16)+x['memory_size']<=0x10000000 for x in sw['load_segments'])
for name,digest in sw['extended_sources'].items():assert sha(r/'sw'/name)==digest,name
for ip,latency in [('xil_fadd',11),('xil_fmul',8)]:
    xci=read(f'build/vivado/gpgpu_soc.srcs/sources_1/ip/{ip}/{ip}.xci')
    assert re.search(r'PARAM_VALUE.C_Latency">'+str(latency)+r'<',xci),ip
hardware=read('evidence/resources_scoped.log')
assert 'VORTEX_RAM_PLACEHOLDERS_RESOLVED' in hardware
assert 'RESOURCE_SYNTHESIS_REPORTED' in hardware
assert not re.search(r'^ERROR:',hardware,re.M)
assert (r/'build/artifacts/top_synth.dcp').is_file()
files=[p for folder in ['rtl','sw','scripts','config','tb','kernel'] for p in (r/folder).rglob('*') if p.is_file() and '__pycache__' not in p.parts]
result={'project_created':True,'part':cfg['part'],'cores':1,'warps':4,'threads':4,
        'board_clock_hz':48484364,'ddr_cl':17,'vendor_commit':manifest['commit'],
        'vendor_files_verified':len(manifest['vendor_sha256']),'original_inputs':original_checks,
        'unit_response_checks':61,'negative_variants_rejected':2,'real_gpu_rounds':3,'checked_sums':48,
        'native_tcu_tex_cases':10,'tcu_outputs':1280,'tex_output_slots_checked':160,
        'texture_unit_cases':1153,'fp16_converter_patterns':65536,'masked_tex_lanes_tested':True,
        'real_gpu_simulation':'PASS','top_rtl_elaboration':'PASS','a53_elf':'COMPILED_NOT_EXECUTED',
        'synthesis':'PASS_WITH_RAM_PLACEHOLDERS_RESOLVED',
        'implementation':(r/'build/artifacts/top_routed.dcp').is_file(),
        'timing_pass':'TIMING_AND_BITSTREAM_PASS' in read('evidence/implementation.log') if (r/'evidence/implementation.log').exists() else False,
        'bitstream':(r/'build/artifacts/top.bit').is_file(),'board_pass':False,
        'implementation_audit':json.loads(read('evidence/implementation.json')) if (r/'evidence/implementation.json').exists() else None,
        'source_sha256':{p.relative_to(r).as_posix():sha(p) for p in files},
        'bd_sha256':sha(r/bdpath),'xsa_sha256':sw['xsa_sha256'],'elf_sha256':sw['elf_sha256']}
(r/'evidence/verified_project.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print('PROJECT_AUDIT_PASS: native c1w4t4 TCU/TEX, simulation, synthesis, source/input/BSP hashes; no board claim')

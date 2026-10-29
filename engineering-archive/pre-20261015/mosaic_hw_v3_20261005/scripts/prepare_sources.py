from pathlib import Path
import json, re, difflib, hashlib
root=Path(__file__).resolve().parents[1]
original=root/'vendor/vortex/hw/rtl'
v=root/'build/compat_rtl'
# Vivado 2020.1 needs explicit token concatenation for macro-generated names.
# Only a derived build copy changes; vendor/ remains byte-identical to v2.3.
patch=[]; changed=[]
for src in original.rglob('*'):
    if not src.is_file(): continue
    dest=v/src.relative_to(original);dest.parent.mkdir(parents=True,exist_ok=True)
    data=src.read_bytes()
    if src.name in ['VX_define.vh','VX_platform.vh','Vortex.sv','VX_tcu_fedp_dsp.sv','VX_tcu_fedp_int.sv']:
        before=data.decode();after=before
        if src.name=='Vortex.sv':
            old='assign mem_req_addr[i]   = mem_bus_if[i].req_data.addr;'
            assert old in before
            # v2.3 forwards a fence as a final read and then drops its flags.
            # Give only that flagged read a dedicated harmless DDR cache line.
            # Ordinary loads (including erroneous null loads) are NOT remapped.
            after=before.replace(old,"assign mem_req_addr[i]   = mem_bus_if[i].req_data.flags[MEM_REQ_FLAG_FLUSH]\n                                  ? VX_MEM_ADDR_WIDTH'(32'h10030000 >> $clog2(`L3_LINE_SIZE))\n                                  : mem_bus_if[i].req_data.addr;")
        elif src.name=='VX_tcu_fedp_int.sv':
            replacements = {
                '    localparam REDW  = `MAX(PRODW + LEVELS, PSELW);':
                    '    // Keep the unsigned guard through every reduction level.\n    localparam REDW  = PSELW + LEVELS;',
                "PSELW'($signed(sum_i8))": "{{(PSELW-18){sum_i8[17]}}, sum_i8}",
                "PSELW'(sum_u8)": "{{(PSELW-18){1'b0}}, sum_u8}",
                "PSELW'($signed(sum_i4))": "{{(PSELW-11){sum_i4[10]}}, sum_i4}",
                "PSELW'(sum_u4)": "{{(PSELW-11){1'b0}}, sum_u4}",
                "REDW'($signed(mult_result[i]))": "{{(REDW-PSELW){mult_result[i][PSELW-1]}}, mult_result[i]}",
                "    wire [31:0] acc = 32'($signed(red_in[LEVELS][0])) + delayed_c;":
                    '    // Explicit bits also preserve the sign in Vivado 2020.1 synthesized logic.\n'
                    '    wire [31:0] red_extended = {{(32-REDW){red_in[LEVELS][0][REDW-1]}}, red_in[LEVELS][0]};\n'
                    '    wire [31:0] acc = red_extended + delayed_c;',
            }
            for old, new in replacements.items():
                assert after.count(old) == 1, ('INT FEDP patch mismatch', old)
                after = after.replace(old, new)
            # Keep the derived file byte-reproducible with this Windows build.
            after = after.replace('\r\n', '\n').replace('\n', '\r\n')
        elif src.name=='VX_tcu_fedp_dsp.sv':
            after=before.replace("8'd127 - 8'd14 - {3'b0, leading_zeros} + 8'd1", "8'd112 - {3'b0, leading_zeros}")
            # Exercise the exact synthesizable Xilinx IP path in XSim too.
            after,count=re.subn(r'`ifndef SYNTHESIS\r?\n`define DSP_TEST\r?\n`endif', '`ifndef SYNTHESIS\n`ifndef Z11_TCU_XIL_IP\n`define DSP_TEST\n`endif\n`endif',after)
            assert count==1,'TCU model selection patch must apply exactly once'
        else:
            after=re.sub(r'(\w+)`__LINE__',r'`Z11_CAT(\1,`__LINE__)',before)
            if after!=before: after='`ifndef Z11_CAT\n`define Z11_CAT_I(a,b) a``b\n`define Z11_CAT(a,b) `Z11_CAT_I(a,b)\n`endif\n'+after
        if after!=before:
            patch.extend(difflib.unified_diff(before.splitlines(True),after.splitlines(True),str(src.relative_to(root)),str(dest.relative_to(root))))
            data=after.encode();changed.append(str(src.relative_to(original)))
    dest.write_bytes(data)
(root/'config/compatibility.patch').write_text(''.join(patch),encoding='utf-8')
(root/'config/compatibility.json').write_text(json.dumps({'reason':'Vivado 2020.1 token concatenation; flagged fence read redirected to reserved DDR 0x10030000','changed':changed},indent=2))
defines=['NDEBUG','VIVADO','XLEN_32','FPU_DSP','EXT_TCU_ENABLE','TCU_DSP','Z11_TCU_XIL_IP','EXT_TEX_ENABLE','NUM_CLUSTERS=1','NUM_CORES=1',
         'SOCKET_SIZE=1','NUM_WARPS=4','NUM_THREADS=4','ICACHE_SIZE=16384',
         'DCACHE_SIZE=16384','DCACHE_WRITEBACK=0','PLATFORM_MEMORY_NUM_BANKS=1',
         'PLATFORM_MEMORY_DATA_SIZE=64','MEM_BLOCK_SIZE=64','MEM_ADDR_WIDTH=32']
sources=[v/'VX_gpu_pkg.sv',v/'fpu/VX_fpu_pkg.sv',v/'tcu/VX_tcu_pkg.sv']
if (v/'VX_trace_pkg.sv').exists(): sources.append(v/'VX_trace_pkg.sv')
skip={'VX_fpu_fpnew.sv','VX_fpu_dpi.sv','VX_tcu_fedp_bhf.sv','VX_tcu_fedp_dpi.sv'}
for folder in ['interfaces','libs','cache','mem','fpu','tcu','core','']:
    for path in sorted((v/folder).glob('*.sv')):
        if path not in sources and path.name not in skip: sources.append(path)
includes=sorted({str(p.parent.relative_to(root)).replace('\\','/') for p in v.rglob('*.vh')})
cfg={'part':'xczu11eg-ffvc1156-2-i','defines':defines,'sources':[str(p.relative_to(root)).replace('\\','/') for p in sources],
     'includes':includes,'axi_data_bits':128,'axi_address_bits':32,'axi_id_bits':6,'banks':1,
     'control_base':'0xA0000000','gpu_ddr_base':'0x10000000','gpu_ddr_size':'0x01000000'}
(root/'config').mkdir(exist_ok=True)
(root/'config/build.json').write_text(json.dumps(cfg,indent=2))
tcl=['set gpu_defines [list '+' '.join(defines)+']','set gpu_sources [list \\']
tcl += ['  [file join $root '+p+'] \\' for p in cfg['sources']]
tcl += [']','set gpu_includes [list \\']
tcl += ['  [file join $root '+p+'] \\' for p in includes]
tcl += [']']
(root/'config/sources.tcl').write_text('\n'.join(tcl)+'\n')
# Retain the pristine upstream file; extract only its standard IP declarations.
src=(root/'vendor/vortex/hw/scripts/xilinx_ip_gen.tcl').read_text()
start=src.index('create_ip -name floating_point')
end=src.index('generate_target all')
(root/'config/fpu_ip.tcl').write_text('# IP declarations from pinned upstream xilinx_ip_gen.tcl; see source_manifest.json\n'+src[start:end]+'\nset_property CONFIG.C_Latency 8 [get_ips xil_fmul]\nset_property CONFIG.C_Latency 11 [get_ips xil_fadd]\n')
print('SOURCE_CONFIG_PASS:',len(sources),'SV files; c1w4t4 RV32IMF and caches')

"""Audit actual generated BD, top, DDR IP and effective GPU configuration."""
from pathlib import Path
import json,re,xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[1]
bdroot=root/'build/vivado/gpgpu_soc.srcs/sources_1/bd/ps_bd'
bd=json.loads((bdroot/'ps_bd.bd').read_text())
rtl=(bdroot/'synth/ps_bd.v').read_text()
top=(root/'rtl/top.sv').read_text()
cfg=(root/'config/sources.tcl').read_text()
checks={}
checks['cdma_reset_connected']=bool(re.search(r'\.s_axi_lite_aresetn\(rst_peripheral_aresetn\)',rtl))
checks['external_ddr_pins']='c0_ddr4_dq' in top and 'c0_sys_clk_p' in top
checks['calibration_two_flops']='reg [1:0] calib_sync' in top and 'memory_ready = calib_sync[1]' in top
checks['four_cores']='NUM_CORES=4' in cfg and 'SOCKET_SIZE=4' in cfg
checks['full_gpu_features']=all(x in cfg for x in ('FPU_DSP','EXT_TCU_ENABLE','EXT_TEX_ENABLE'))
checks['lmem_reserved']='LMEM_BASE_ADDR=1879048192' in cfg
checks['gpu_axi_512']='.AXI_DATA_WIDTH(512)' in (root/'rtl/gpgpu_subsystem.sv').read_text()
checks['pin_count_117']=(root/'board/dual_ddr.xdc').read_text().count('PACKAGE_PIN')==117
xci=bdroot/'ip/ps_bd_ddr_0/ps_bd_ddr_0.xci'
ns='{http://www.spiritconsortium.org/XMLSchema/SPIRIT/1685-2009}'
for instance, ext_polarity in [('rst', '0'), ('rst_ddr', '1')]:
    reset_xci=bdroot/f'ip/ps_bd_{instance}_0/ps_bd_{instance}_0.xci'
    reset_params={e.attrib.get(ns+'referenceId'):e.text for e in ET.parse(reset_xci).iter(ns+'configurableElementValue')}
    block=re.search(r'ps_bd_'+instance+r'_0\s+'+instance+r'\s*\((.*?)\);',rtl,re.S)
    assert block, f'Reset instance missing: {instance}'
    checks[instance+'_aux_active_low']=reset_params.get('PARAM_VALUE.C_AUX_RESET_HIGH')=='0'
    checks[instance+'_ext_polarity']=reset_params.get('PARAM_VALUE.C_EXT_RESET_HIGH')==ext_polarity
    checks[instance+'_aux_tied_inactive']=bool(re.search(r'\.aux_reset_in\(const_one_dout\)',block[1]))
    checks[instance+'_debug_tied_inactive']=bool(re.search(r'\.mb_debug_sys_rst\(const_zero_dout\)',block[1]))
params={e.attrib.get(ns+'referenceId'):e.text for e in ET.parse(xci).iter(ns+'configurableElementValue')}
expected={'C0.DDR4_AxiSelection':'true','C0.DDR4_AxiDataWidth':'512','C0.DDR4_AxiAddressWidth':'32','C0.DDR4_DataWidth':'64','C0.DDR4_TimePeriod':'833','C0.DDR4_InputClockPeriod':'4998','C0.DDR4_CasLatency':'18','C0.DDR4_CasWriteLatency':'12','C0.DDR4_MemoryPart':'MT40A512M16HA-083E'}
actual={k:params.get('PARAM_VALUE.'+k) for k in expected}
for k,v in expected.items():checks['ddr_'+k]=actual[k]==v
out={'checks':checks,'ddr_parameters':actual,'all_pass':all(checks.values())}
(root/'evidence/generated_audit.json').write_text(json.dumps(out,indent=2))
print(json.dumps(out,indent=2))
if not out['all_pass']:raise SystemExit('Generated hardware audit failed; inspect individual checks')

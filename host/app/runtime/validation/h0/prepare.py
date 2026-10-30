"""Fresh CNN host adapter on hash-checked frozen actual H0 compiled RTL."""
import argparse,hashlib,json,shutil,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/cnn'))
from source_identity import source_commit
from generate_fixtures import CASES,generate_fixture
from generate_kernels import generate_program
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def prepare(run_id,manifest_path):
 if not run_id or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.' for c in run_id):raise ValueError('Bad run ID')
 manifest=json.loads(Path(manifest_path).read_text())
 if manifest['source_commit']!=source_commit(ROOT):raise ValueError('Source commit mismatch')
 old=ROOT.parent/'vortex_v23_dual_ddr_loopback_20261001';r4=ROOT.parent/'vortex_v23_dual_ddr_functional_r4_20261003'
 frozen=json.loads((old/'evidence/simulation_inputs.json').read_text())
 if not json.loads((old/'evidence/result.json').read_text())['pass']:raise ValueError('Baseline not verified')
 for v in frozen['files']:
  if sha(Path(v['path']))!=v['sha256']:raise ValueError('Changed frozen input '+v['path'])
 wd=ROOT/'build'/run_id/'rtl';wd.mkdir(parents=True,exist_ok=False)
 shutil.copytree(old/'build/xsim/xsim.dir/xil_defaultlib',wd/'xsim.dir/xil_defaultlib')
 for p in (old/'build/xsim').iterdir():
  if p.is_file() and (p.suffix=='.mem' or p.name=='xsim.ini'):shutil.copyfile(p,wd/p.name)
 (wd/'tb_cnn_h0.sv').write_text((old/'tb/tb_ps_pl_loopback.sv').read_text().replace('tb_ps_pl_loopback','tb_cnn_h0').replace('ps_host_tests.svh','cnn_adapter.svh'))
 original=(old/'tb/ps_host_tests.svh').read_text()
 prefix=original[:original.index('task automatic checkreg')].replace('tb_ps_pl_loopback','tb_cnn_h0').replace('ps_pl_loopback.vcd','cnn_h0.vcd')
 marker='  if(response!==0) $fatal(1,"LOOPBACK_FAIL WRITE_RESPONSE addr=%h resp=%b",addr,response);'
 prefix=prefix.replace(marker,marker+"""
 if(addr==CDMA+40'h28) dma_jobs++;
 if(addr==GPU+40'h8 && value==2) begin
  if(exit_warps_seen!==16'hffff) $fatal(1,"CNN_MISSING_EXIT_WARPS mask=%h",exit_warps_seen);
  if(gpu_flush_issued!=(gpu_rounds+1)*16 || ps_flush_accepted!=gpu_flush_issued) $fatal(1,"CNN_FENCE_COUNT");
  $display("CNN_RTL_THREADS_PASS launch=%0d threads=64 warps=16 fence=%0d",gpu_rounds+1,gpu_flush_issued);
  gpu_rounds++;exit_warps_seen=0;
 end""")
 monitor="reg [15:0] exit_warps_seen=0;\n"
 for c in range(4):
  ctl=f'dut.pl.gpu.vortex.g_clusters[0].cluster.g_sockets[0].socket.g_cores[{c}].core.warp_ctl_if'
  monitor+=f'always @(posedge dut.pl_clk0) if(phase==4 && {ctl}.valid && {ctl}.tmc.valid && {ctl}.tmc.tmask==0) begin\n if(exit_warps_seen[{c}*4+{ctl}.wid]) $fatal(1,"CNN_DUPLICATE_WARP_EXIT");\n exit_warps_seen[{c}*4+{ctl}.wid]=1;\nend\n'
 ps=original[original.index('task automatic pswrite'):original.index('task automatic pscheck')]
 tail='''
@TICK@include "host_access.svh"
import "DPI-C" context task host_entry(output int result);
export "DPI-C" task host_bus;
initial begin : cnn_sequence
 int result;#1ns;
 @TICK@PSVIP.set_debug_level_info(0);@TICK@PSVIP.set_stop_on_error(1);
 @TICK@PSVIP.fpga_soft_reset(32'h1);@TICK@PSVIP.por_srstb_reset(1'b0);
 #1us;@TICK@PSVIP.por_srstb_reset(1'b1);#1us;
 if(@TICK@PSVIP.net_rstn!==1 || @TICK@PSVIP.ddrc.state!==2'd0 || @TICK@PSVIP.S_AXI_HP0_FPD.rd_fifo_state!==1'b0) $fatal(1,"CNN_PS_POR");
 @TICK@PSVIP.fpga_soft_reset(32'h0);wait(resetn_observed===1'b1);
 host_entry(result);
 if(result!=0) $fatal(1,"CNN_HOST_FAILED result=%0d",result);
 if(gpu_rounds!=20 || gpu_flush_issued!=320 || ps_flush_accepted!=320 || dma_jobs==0 || hpm0_reads==0 || hpm1_reads==0 || hp_reads==0 || ddr_reads==0 || gpu_reads==0 || gpu_writes==0) $fatal(1,"CNN_BOUNDARIES");
 $display("CNN_FULL_SOC_PASS launches=20 threads_per_launch=64 fence_source=320 fence_hp=320");
 $dumpflush;$finish;
end
'''.replace('@TICK@',chr(96))
 (wd/'cnn_adapter.svh').write_text(monitor+prefix+ps+tail)
 alloc=(old/'tb/ps_allocation_check.svh').read_text().replace("last_byte<=64'h10000080","last_byte<=64'h10001000")
 (wd/'ps_allocation_check.svh').write_text(alloc)
 shutil.copyfile(r4/'tb/host_access.svh',wd/'host_access.svh')
 for name in ('host_platform.h','gpgpu_hw.h'):shutil.copyfile(r4/'sw'/name,wd/name)
 shutil.copyfile(ROOT/'validation/h0/cnn_host.c',wd/'cnn_host.c')
 dpi=(r4/'sw/dpi_backend.c').read_text().replace('../build/xsim/host_dpi.h','host_dpi.h').replace('host_main()','cnn_rtl_host_run()').replace('static uint32_t access_', 'int cnn_rtl_host_run(void);\nstatic uint32_t access_')
 (wd/'dpi_backend.c').write_text(dpi);shutil.copyfile(r4/'build/xsim/host_dpi.h',wd/'host_dpi.h')
 project=(old/'build/xsim/loopback_vlog.prj').read_text()
 line=next(x for x in project.splitlines() if '/tb/tb_ps_pl_loopback.sv"' in x).replace((old/'tb').as_posix(),wd.as_posix()).replace('tb_ps_pl_loopback.sv','tb_cnn_h0.sv')
 (wd/'cnn_tb.prj').write_text(line+'\nnosort\n')
 shutil.copytree(ROOT/'kernel/cnn',wd/'programs');(wd/'outputs').mkdir()
 for case in CASES:generate_fixture(case,wd/'fixtures'/case)
 for item in manifest['programs']:
  p=wd/f"programs/id{item['id']}.bin"
  if sha(p)!=item['sha256'] or p.read_bytes()!=generate_program(item['id']):raise ValueError('Program SHA')
 inputs=[dict(path=str(p),sha256=sha(p),bytes=p.stat().st_size) for p in sorted(wd.rglob('*')) if p.is_file()]+frozen['files']
 for p in sorted((ROOT/'validation/h0').glob('*')):
  if p.is_file():inputs.append(dict(path=str(p),sha256=sha(p),bytes=p.stat().st_size))
 tools=[Path('G:/xilinx/Vivado/2020.1/bin')/(name+'.bat') for name in ('xvlog','xelab','xsim')]
 tools+=[Path('G:/xilinx/Vivado/2020.1/tps/mingw/6.2.0/win64.o/nt/bin')/name for name in ('gcc.exe','ar.exe')]
 for p in tools:inputs.append(dict(path=str(p),sha256=sha(p),bytes=p.stat().st_size))
 (wd/'inputs.json').write_text(json.dumps(dict(scope='CNN_H0_RTL_INPUTS',source_commit=source_commit(ROOT),programs=manifest['programs'],inputs=inputs),indent=2))
 print(wd,flush=True);return wd
if __name__=='__main__':
 a=argparse.ArgumentParser();a.add_argument('--run-id',required=True);a.add_argument('--manifest',required=True);o=a.parse_args();prepare(o.run_id,o.manifest)

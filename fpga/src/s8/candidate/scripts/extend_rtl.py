"""Apply reproducible native-TEX integration only to derived v2.3 files."""
from pathlib import Path
import re,difflib,json
r=Path(__file__).resolve().parents[1];d=r/'build/compat_rtl'
def edit(file,fn):
 p=d/file;s=p.read_text(encoding='utf-8');t=fn(s);assert t!=s,file;p.write_text(t,encoding='utf-8')
def sub(s,a,b):
 assert a in s,a
 return s.replace(a,b)
edit('VX_gpu_pkg.sv',lambda s:sub(sub(s,'localparam NUM_EX_UNITS = EX_TCU + 1;', 'localparam EX_TEX = EX_TCU + 1;\n    localparam NUM_EX_UNITS = EX_TEX + 1;'),'L1_MEM_TAG_WIDTH + `CLOG2(2)','L1_MEM_TAG_WIDTH + `CLOG2(3)'))
edit('core/VX_decode.sv',lambda s:sub(s,'            INST_EXT1: begin','''            INST_EXT2: begin
                if (funct7 == 0 && funct3 == 0) begin
                    ex_type = EX_TEX;
                    op_type = '0;
                    `USED_IREG (rd);
                    `USED_IREG (rs1);
                    `USED_IREG (rs2);
                end
            end
            INST_EXT1: begin'''))
def execute(s):
 s=sub(s,'    // Dcache interface','''    VX_dcr_bus_if.slave dcr_bus_if,
    VX_mem_bus_if.master tex_mem_if,
    output wire tex_pending, tex_error,
    // Dcache interface''')
 return sub(s,'    VX_sfu_unit #(','''    VX_tex_unit tex_unit (
        .clk(clk), .reset(reset), .dcr_bus_if(dcr_bus_if),
        .dispatch_if(dispatch_if[EX_TEX * `ISSUE_WIDTH]),
        .commit_if(commit_if[EX_TEX * `ISSUE_WIDTH]),
        .tex_mem_if(tex_mem_if), .tex_pending(tex_pending), .tex_error(tex_error)
    );

    VX_sfu_unit #(''')
edit('core/VX_execute.sv',execute)
def core(s):
 s=sub(s,'    VX_mem_bus_if.master    icache_bus_if,','''    VX_mem_bus_if.master    icache_bus_if,
    VX_mem_bus_if.master    tex_mem_if,
    output wire tex_pending, tex_error,''')
 s=sub(s,'    VX_schedule_if      schedule_if();','''    wire schedule_busy;
    assign busy = schedule_busy | tex_pending;
    VX_schedule_if      schedule_if();''')
 s=sub(s,'.busy           (busy)','.busy           (schedule_busy)')
 return sub(s,'        .lsu_mem_if     (lsu_mem_if),','''        .dcr_bus_if(dcr_bus_if), .tex_mem_if(tex_mem_if),
        .tex_pending(tex_pending), .tex_error(tex_error),
        .lsu_mem_if     (lsu_mem_if),''')
edit('core/VX_core.sv',core)
def socket(s):
 s=sub(s,'    // Status','    output wire tex_pending, tex_error,\n    // Status')
 s=sub(s,'    wire [`SOCKET_SIZE-1:0] per_core_busy;','''    wire [`SOCKET_SIZE-1:0] per_core_busy;
    wire [`SOCKET_SIZE-1:0] tex_busy_vec,tex_error_vec;
    assign tex_pending=|tex_busy_vec;assign tex_error=|tex_error_vec;''')
 s=sub(s,'    for (genvar i = 0; i < `L1_MEM_PORTS;', '''    `STATIC_ASSERT (`SOCKET_SIZE == 1 && `L1_MEM_PORTS == 1, ("TEX single-core memory contract"))
    VX_mem_bus_if #(.DATA_SIZE(64), .TAG_WIDTH(L1_MEM_TAG_WIDTH)) tex_mem_bus_if();
    for (genvar i = 0; i < `L1_MEM_PORTS;''')
 s=sub(s,') l1_mem_bus_if[2]();',') l1_mem_bus_if[3]();')
 s=sub(s,'            VX_mem_arb #(','''            `ASSIGN_VX_MEM_BUS_IF(l1_mem_bus_if[2], tex_mem_bus_if);
            VX_mem_arb #(''')
 s=sub(s,'.NUM_INPUTS (2)', '.NUM_INPUTS (3)')
 s=sub(s,'.ARBITER    ("P"), // prioritize the icache','.ARBITER    ("R"), // fair I/D/TEX service')
 return sub(s,'            .icache_bus_if  (per_core_icache_bus_if[core_id]),','''            .icache_bus_if  (per_core_icache_bus_if[core_id]),
            .tex_mem_if(tex_mem_bus_if),
            .tex_pending(tex_busy_vec[core_id]), .tex_error(tex_error_vec[core_id]),''')
edit('VX_socket.sv',socket)
def outer(s,vec,dim,index):
 s=sub(s,'    // Status','    output wire tex_pending, tex_error,\n    // Status')
 s=sub(s,f'    wire [{dim}-1:0] {vec};',f'''    wire [{dim}-1:0] {vec};
    wire [{dim}-1:0] tex_busy_vec,tex_error_vec;
    assign tex_pending=|tex_busy_vec;assign tex_error=|tex_error_vec;''')
 s=re.sub(r'(\s+\.busy\s+\('+re.escape(vec)+r'\['+index+r'\]\))',f'\n            .tex_pending(tex_busy_vec[{index}]), .tex_error(tex_error_vec[{index}]),'+r'\1',s)
 return s
edit('VX_cluster.sv',lambda s:sub(outer(s,'per_socket_busy','NUM_SOCKETS','socket_id'),'is_base_dcr_addr, (NUM_SOCKETS > 1)',"(is_base_dcr_addr || (dcr_bus_if.write_addr >= 12'h100 && dcr_bus_if.write_addr <= 12'h104)), (NUM_SOCKETS > 1)"))
edit('Vortex.sv',lambda s:outer(s,'per_cluster_busy','`NUM_CLUSTERS','cluster_id'))
def axi(s):
 s=re.sub(r'(    output wire\s+busy)',r'    output wire tex_pending, tex_error,\n\1',s,count=1)
 return sub(s,'        .busy           (busy)','        .tex_pending(tex_pending), .tex_error(tex_error),\n        .busy           (busy)')
edit('Vortex_axi.sv',axi)
patch=[];changed=[]
for src in (r/'vendor/vortex/hw/rtl').rglob('*'):
 if not src.is_file():continue
 dest=d/src.relative_to(r/'vendor/vortex/hw/rtl')
 if src.read_bytes()==dest.read_bytes():continue
 a=src.read_text(encoding='utf-8');b=dest.read_text(encoding='utf-8')
 if a!=b:
  changed.append(str(src.relative_to(r/'vendor/vortex/hw/rtl')))
  patch.extend(difflib.unified_diff(a.splitlines(True),b.splitlines(True),str(src.relative_to(r)),str(dest.relative_to(r))))
(r/'config/compatibility.patch').write_text(''.join(patch),encoding='utf-8')
(r/'config/compatibility.json').write_text(json.dumps({'changed':changed,'extensions':['TCU','native TEX']},indent=2))
print('NATIVE_TEX_DERIVATIVE_READY',len(changed),'upstream files')

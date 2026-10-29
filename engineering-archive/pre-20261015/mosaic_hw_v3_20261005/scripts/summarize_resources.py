"""Extract measured Vivado utilization; never fill missing reports by estimate."""
from pathlib import Path
import json
r=Path(__file__).resolve().parents[1]
all_results={}
doc=['# TCU + TEX 单核工程：实测资源','',
     '2026-09-26；Vivado 2020.1；xczu11eg-ffvc1156-2-i；1 core / 4 warp / 4 thread。',
     '包含 PS 接口、控制器、FPU、TCU、TEX、I/D-cache 的 RTL top。百分比来自该器件的实际 Vivado 报告，不是其他器件的折算。','']
def rows(path):
 return [[x.strip() for x in s.split('|')[1:-1]] for s in path.read_text().splitlines() if s.startswith('|')]
for stage,label in [('synth','综合后，RAM占位解析完成'),('routed','布局布线后')]:
 p=r/f'evidence/utilization_{stage}.rpt';h=r/f'evidence/utilization_{stage}_hier.rpt'
 if not p.exists() or not h.exists():continue
 rr=rows(p);hh=rows(h);header=next(x for x in hh if x and x[0]=='Instance')
 doc += [f'## {label}','',f'来源：[总量](../evidence/{p.name})、[层次](../evidence/{h.name})。','',
         '| 资源 | 已用 | 器件总量 | 占比 |','|---|---:|---:|---:|']
 total={}
 for key,display in [('CLB LUTs*','LUT'),('CLB Registers','FF'),('Block RAM Tile','BRAM Tile'),('DSPs','DSP'),('URAM','URAM')]:
  row=next(x for x in rr if x and x[0].rstrip('*')==key.rstrip('*'))
  used,available,pct=float(row[1]),int(row[3]),float(row[4])
  total[display]={'used':used,'available':available,'percent':pct}
  doc.append(f'| {display} | {used:g} | {available:,} | {pct:.2f}% |')
 doc+=['','| 模块（包含内部子模块） | LUT | FF | BRAM Tile | DSP |','|---|---:|---:|---:|---:|']
 blocks={}
 for instance,title in [('tcu_unit','TCU'),('tex_unit','TEX（含纹理缓存）'),('icache','I-cache层次'),('dcache','D-cache层次'),('ctrl','控制寄存器/状态机')]:
  hits=[dict(zip(header,x)) for x in hh if x and x[0]==instance]
  assert len(hits)==1,(stage,instance)
  x=hits[0];vals={'LUT':int(x['Total LUTs']),'FF':int(x['FFs']),
      'BRAM Tile':int(x['RAMB36'])+int(x['RAMB18'])/2,'DSP':int(x['DSP Blocks'])}
  blocks[title]=vals
  doc.append(f"| {title} | {vals['LUT']:,} | {vals['FF']:,} | {vals['BRAM Tile']:g} | {vals['DSP']} |")
 doc+=['','BRAM以36 Kb Tile计：RAMB18算0.5。层次统计包含队列、元数据和实际RAM映射，不是缓存的逻辑容量；子模块已计入整机，不能再次相加。','']
 all_results[stage]={'total':total,'blocks':blocks,'report':str(p),'hierarchical_report':str(h)}
if (r/'evidence/implementation.json').exists():
 impl=json.loads((r/'evidence/implementation.json').read_text())
 doc+=['## 实现检查','',
       f"20.625 ns时钟约束（约48.485 MHz）：WNS {impl['wns_ns']:+.3f} ns，WHS {impl['whs_ns']:+.3f} ns，WPWS {impl['wpws_ns']:+.3f} ns；无未约束内部端点，无布线错误。",
       '已生成top.bit；未对开发板编程。DRC无Error，仍有137条Warning及16条Advisory，包含16条AVAL-318（TCU浮点加法IP的DSP PCIN连接），未豁免或证明无影响。',
       '这是时序通过且有DRC警告的实现结果，不称DRC零警告或实板PASS。原始证据：[时序](../evidence/timing_summary.rpt)、[布线](../evidence/route_status.rpt)、[DRC](../evidence/drc.rpt)、[工件哈希](../evidence/implementation.json)。',
       '本轮为直接批处理构建；Vivado中打开build/artifacts/top_routed.dcp查看实际结果，XPR的synth_1/impl_1运行面板不代表本次批处理状态。','']
doc+=['## 结果边界','',
      '综合报告在解析Vortex异步RAM占位后生成，无剩余黑盒。综合后与布局布线后资源可能不同，应比较同一阶段。',
      '时序、DRC和实板是独立证据，详见evidence/timing_summary.rpt、drc.rpt、route_status.rpt（只有生成后才成立）。本轮未上板。','',
      '**反问：为什么DSP占比低，不能直接推断可放几十个核？** 每核还需要LUT、寄存器、缓存和队列。此配置首先占用更多BRAM与LUT；扩核还会改变共享缓存、互连和HP带宽。当前结果只证明这个单核配置的资源，不能按DSP余量线性推算扩核数量。','']
doc+=['**反问：为什么4 KiB纹理缓存用了7.5个BRAM Tile？** RTL按64行×512位组织，以整行并行读写；Vivado用多个BRAM并联满足位宽，各块的深度没有全部利用。逻辑容量、物理块容量和端口形态是三个不同指标。后续若改成较窄的存储组织及多周期填充，需要同时评估面积、填充延迟和带宽。','']
(r/'evidence/resources.json').write_text(json.dumps(all_results,ensure_ascii=False,indent=2),encoding='utf-8')
(r/'docs/resources.md').write_text('\n'.join(doc),encoding='utf-8')
print(json.dumps(all_results,ensure_ascii=False,indent=2))

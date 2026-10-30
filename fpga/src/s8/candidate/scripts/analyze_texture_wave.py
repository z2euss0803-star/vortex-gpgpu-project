"""Check edge-sampled request/response stability in the actual unit-test VCD."""
from pathlib import Path
import json
r=Path(__file__).resolve().parents[1];o=r/'evidence/texture'
names={};value={};previous={};held={};counts={};edges=0
channels={'sample':('req_valid','req_ready',['req_u','req_v']),
          'pixel':('rsp_valid','rsp_ready',['rsp_rgba','rsp_error']),
          'memory_request':('mem_req_valid','mem_req_ready',['mem_req_addr']),
          'memory_response':('mem_rsp_valid','mem_rsp_ready',['mem_rsp_data'])}
def val(v,n):return v.get(names[n],'x')
def tick():
 global edges
 if not names or val(previous,'clk')!='0' or val(value,'clk')!='1':return
 edges+=1
 if val(previous,'reset')!='0':held.clear();return
 for name,(valid,ready,data) in channels.items():
  active=val(previous,valid)=='1';accepted=val(previous,ready)=='1'
  payload=tuple(val(previous,d) for d in data)
  if name in held:assert active and payload==held[name],f'Unstable {name}, edge {edges}'
  held.pop(name,None)
  if active and not accepted:held[name]=payload
  if active and accepted:counts[name]=counts.get(name,0)+1
with (o/'texture.vcd').open() as f:
 for line in f:
  line=line.strip()
  if line.startswith('$var'):
   parts=line.split();names[parts[4]]=parts[3]
  elif line.startswith('#'):
   tick();previous=value.copy()
  elif line and line[0] in '01xz':value[line[1:]]=line[0]
  elif line.startswith('b'):
   bits,symbol=line[1:].split();value[symbol]=bits
tick()
assert counts=={'sample':1156,'pixel':1156,'memory_request':1315,'memory_response':1315},counts
result={'status':'PASS','sampled_rising_edges':edges,'handshakes':counts,'stability_under_stall':'PASS'}
(o/'wave_analysis.json').write_text(json.dumps(result,indent=2))
print('TEXTURE_WAVE_PASS',counts)

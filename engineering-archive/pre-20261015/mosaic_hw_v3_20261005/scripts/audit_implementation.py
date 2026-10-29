"""Separate route/timing success from DRC warnings and board evidence."""
from pathlib import Path
import re,json,hashlib
r=Path(__file__).resolve().parents[1]
def text(name):return (r/'evidence'/name).read_text()
timing=text('timing_summary.rpt');route=text('route_status.rpt');drc=text('drc.rpt')
values=next(s.split() for s in timing.splitlines() if re.match(r'^\s*[-+]?\d+\.\d+\s+[-+]?\d+\.\d+',s))
assert 'All user specified timing constraints are met.' in timing
assert all(int(values[n])==0 for n in [2,6,10])
assert 'checking no_clock (0)' in timing and 'checking unconstrained_internal_endpoints (0)' in timing
assert re.search(r'nets with routing errors\.+\s*:\s*0\s*:',route)
rules=[]
for s in drc.splitlines():
 if not s.startswith('|'):continue
 row=[x.strip() for x in s.split('|')[1:-1]]
 if len(row)==4 and re.fullmatch(r'[A-Z]+-\d+',row[0]):
  rules.append({'rule':row[0],'severity':row[1],'description':row[2],'count':int(row[3])})
assert rules and not any(x['severity'] in ['Error','Critical Warning'] for x in rules)
bit_name='top_int8_signext_fix_20260927.bit'
bit=r/'build/artifacts'/bit_name
assert 'TIMING_AND_BITSTREAM_PASS: board execution not performed' in text('intfix_implementation.log')
assert bit.is_file() and bit.stat().st_size>1000
artifacts={}
for name in ['top_before_ram_fix.dcp','top_synth.dcp','top_routed.dcp',bit_name]:
 p=r/'build/artifacts'/name
 artifacts[name]={'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
result={'part':'xczu11eg-ffvc1156-2-i','clock_period_ns':20.625,
        'wns_ns':float(values[0]),'whs_ns':float(values[4]),'wpws_ns':float(values[8]),
        'setup_hold_pulse_width_pass':True,'unconstrained_internal_endpoints':0,
        'routing_errors':0,'drc_errors':0,'drc_rules':rules,
        'drc_warning_free':False,'aval_318_disposition':'OPEN - DSP PCIN connectivity not waived',
        'board_pass':False,'artifacts':artifacts,
        'build_log':'evidence/intfix_implementation.log',
        'source_sha256':hashlib.sha256((r/'build/compat_rtl/tcu/VX_tcu_fedp_int.sv').read_bytes()).hexdigest()}
(r/'evidence/implementation.json').write_text(json.dumps(result,indent=2))
print('IMPLEMENTATION_AUDIT_PASS: route and timing pass, bit written; DRC warnings remain; not board proof')
print(json.dumps({k:v for k,v in result.items() if k!='artifacts'},indent=2))

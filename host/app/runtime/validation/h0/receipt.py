"""Audit bound process, program, boundary and full-object output evidence."""
import hashlib,json,re
from pathlib import Path
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def verify(wd,r):
 errors=[]
 try:
  if r['scope']!='CNN_H0_FULL_SOC_RTL' or r['natural_exit'] is not True:errors.append('scope/natural_exit')
  if not re.fullmatch('[0-9a-f]{40}',r['source_commit']):errors.append('source_commit')
  stages=r['stages']
  if set(stages)!=set(('compile','dpi-host','dpi-backend','dpi','elaborate','simulate')):errors.append('stages')
  for k,s in stages.items():
   if s['exit']!=0 or sha(wd/s['log'])!=s['sha256']:errors.append('process '+k)
  inputs=json.loads((wd/'inputs.json').read_text())
  if inputs['source_commit']!=r['source_commit'] or not inputs['inputs']:errors.append('inputs/source')
  for item in inputs['inputs']:
   if sha(Path(item['path']))!=item['sha256']:errors.append('input changed')
  if inputs['programs']!=r['programs']:errors.append('programs')
  for p in r['programs']:
   if sha(wd/f"programs/id{p['id']}.bin")!=p['sha256']:errors.append('program SHA')
  wave=wd/r['waveform']['path']
  if wave.stat().st_size<1000 or sha(wave)!=r['waveform']['sha256']:errors.append('waveform')
  log=(wd/'simulate.log').read_text(errors='replace')
  if 'CNN_FULL_SOC_PASS launches=20 threads_per_launch=64 fence_source=320 fence_hp=320' not in log or '$finish called' not in log or re.search(r'(?im)(?:^CNN_FAIL|^Fatal:|^ERROR:)',log):errors.append('natural termination')
  threads=re.findall(r'CNN_RTL_THREADS_PASS launch=(\d+) threads=64 warps=16 fence=(\d+)',log)
  if threads!=[(str(i),str(i*16)) for i in range(1,21)]:errors.append('threads/fence/launches')
  expected_layers=[];expected_outputs={}
  for ci,case in enumerate(('main','zero','negative','positive','checker')):
   fixture=json.loads((wd/'fixtures'/case/'manifest.json').read_text())
   for id in range(2,6):
    elements={2:72,3:72,4:18,5:2}[id];jobs=ci*4+id-1
    expected_layers.append(f'CNN_LAYER_PASS case={case} id={id} output_elements={elements} objects=9 guards=PASS jobs={jobs} fence=PASS')
    for t in fixture['tensors']:
     done={'z':2,'r':3,'p':4,'y':5}.get(t['name'],99)<=id
     want=wd/'fixtures'/case/(t['expected_file'] if done else t['file'])
     output=wd/'outputs'/f"{case}-id{id}-{t['name']}.bin"
     if output.read_bytes()!=want.read_bytes():errors.append('outputs/guards '+output.name)
     expected_outputs[output.name]=sha(output)
  if re.findall(r'CNN_LAYER_PASS[^\r\n]+',log)!=expected_layers:errors.append('layer inventory')
  if r['outputs']!=expected_outputs:errors.append('outputs hashes/inventory')
 except (KeyError,ValueError,OSError,TypeError) as e:errors.append('Incomplete receipt: '+str(e))
 return errors

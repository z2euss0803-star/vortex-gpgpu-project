"""Exact 512-byte contract. No generated/self hash may enter the source input."""
import re,struct
ORDER=('software_source','device_flat','device_elf','layout','parameters','cases','rv32_compiler','rv32_runtime','runner','kernel_config','hardware_source','routed_dcp','bit','xsa')
def make_binding(inputs):
 if set(inputs)!=set(ORDER):raise ValueError('exact fourteen DAG inputs required; self hashes forbidden')
 for name in ORDER:
  value=inputs[name]
  if type(value) is not str or not re.fullmatch('[0-9a-f]{64}',value) or value=='0'*64:raise ValueError('missing/invalid digest '+name)
 return struct.pack('<8I',0x31423853,1,512,5,11,1,1,0)+b''.join(bytes.fromhex(inputs[n]) for n in ORDER)+bytes(32)

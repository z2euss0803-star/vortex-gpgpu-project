"""Acyclic MosaicBindingV1 format; not source admission or a runnable release."""
import re,struct
ORDER=('software_source','device_flat','device_elf','model','assets','graph_catalog','layout_catalog','rv32_compiler','rv32_runtime','runner','kernel_config','hardware_manifest','bit','xsa')
def make_binding(inputs):
    if type(inputs) is not dict or set(inputs)!=set(ORDER):raise ValueError('exact14 DAG inputs, no self hash')
    for name in ORDER:
        value=inputs[name]
        if type(value) is not str or not re.fullmatch('[0-9a-f]{64}',value) or value=='0'*64:raise ValueError('missing or invalid '+name)
    return struct.pack('<8I',0x31424d56,1,512,6,12,1,1,0)+b''.join(bytes.fromhex(inputs[name]) for name in ORDER)+bytes(32)

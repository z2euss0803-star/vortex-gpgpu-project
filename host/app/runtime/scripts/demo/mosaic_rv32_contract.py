"""Private target layout contract; validation is not target execution."""
def validate_runtime(p):
    if p.get('entry')!=0x80000000 or p.get('crt_base')!=0x10002000 or p.get('crt_bytes')!=4096 or p.get('fcsr_init')!=0:raise ValueError('entry/private CRT/RNE contract')
    if p.get('stack_base')!=0xfff00000 or p.get('stack_bytes')!=0x100000 or p.get('stack_stride')!=8192 or not 0<p.get('static_stack_bytes',0)<8128:raise ValueError('stack envelope')
    if p.get('rodata_gpu')!=0x80110000 or p.get('rodata_bytes')!=6307840 or p['rodata_gpu']+p['rodata_bytes']>0x80800000:raise ValueError('read-only sidecar range')
    end,stride=p.get('tls_base',0),p.get('tbss_stride',0)
    if end<0x80000000 or end%16 or not 0<stride<=4096 or stride%16 or end+64*stride>0x800fffc0:raise ValueError('TLS intersects runtime guards')
    if p.get('runtime_bytes')!=(end-0x80000000+64*stride+63)//64*64:raise ValueError('runtime extent')
    return p

def plan_runtime(end_offset,tbss_size,static_stack_bytes):
    if type(end_offset)!=int or type(tbss_size)!=int or type(static_stack_bytes)!=int:raise ValueError('integer metadata')
    stride=(tbss_size+15)//16*16
    return validate_runtime(dict(entry=0x80000000,crt_base=0x10002000,crt_bytes=4096,fcsr_init=0,stack_base=0xfff00000,stack_bytes=0x100000,stack_stride=8192,static_stack_bytes=static_stack_bytes,tls_base=0x80000000+end_offset,tbss_bytes=tbss_size,tbss_stride=stride,tbss_align=16,runtime_bytes=(end_offset+64*stride+63)//64*64,rodata_gpu=0x80110000,rodata_bytes=6307840))

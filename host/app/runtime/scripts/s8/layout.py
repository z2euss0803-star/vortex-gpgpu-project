"""Fixed hardware domains with dynamic ELF runtime bounds, never Linux pointers."""
from pathlib import Path
import json
from scripts.s8.catalog import expected_cases

ROOT=Path(__file__).resolve().parents[2]
FIXED=json.loads((ROOT/'contracts/s8/layout.json').read_text(encoding='utf-8'))

def _integer(value,minimum=0,maximum=0xffffffff):
    if type(value) is not int or not minimum<=value<=maximum:raise ValueError('integer out of range')
    return value

def build_layout(elf: dict,catalog: dict) -> dict:
    if sorted(catalog.get('cases',[]),key=lambda r:r['id'])!=sorted(expected_cases(),key=lambda r:r['id']):raise ValueError('case contract differs')
    if elf.get('entry')!=0x80000000:raise ValueError('only high-window entry supported')
    stride=_integer(elf['tbss_stride'],1);size=_integer(elf['tbss_bytes'],1)
    align=_integer(elf['tbss_align'],1);base=_integer(elf['tls_base'])
    if elf['tdata_bytes']!=0 or type(elf['tdata_bytes']) is not int:raise ValueError('TDATA forbidden')
    if align&(align-1) or stride%align or stride<size or base%align:raise ValueError('TLS alignment/stride')
    _integer(elf['static_stack_bytes'],1,8127)
    if base<0x80000000 or base+64*stride>0x800f0000:raise ValueError('TLS exceeds runtime region')
    ranges=[]
    for row in elf['load_ranges']:
        start=_integer(row['start']);mem=_integer(row['mem_bytes'],1);file_size=_integer(row['file_bytes'])
        if file_size>mem or start<0x80000000 or start+mem>0x800f0000:raise ValueError('PT_LOAD bounds')
        ranges.append((start,start+mem))
    if not ranges:raise ValueError('complete ELF loads required')
    ordered=sorted(ranges)
    if any(a[1]>b[0] for a,b in zip(ordered,ordered[1:])) or max(r[1] for r in ranges)>base:raise ValueError('ELF/TLS overlap')
    result=json.loads(json.dumps(FIXED))
    objects=[]
    for name,(begin,end) in result['regions'].items():
        slots=16 if name in ('A','B','OUT') else 1
        stride_bytes=(end-begin)//slots
        for slot in range(slots):objects.append(dict(region=name,slot=slot,begin=begin+slot*stride_bytes,bytes=stride_bytes))
    result.update(objects=objects,objects_count=len(objects),elf=dict(elf),runtime_bytes=base+64*stride-0x80000000,
                  tls_ranges=[[base+h*stride,base+h*stride+size] for h in range(64)],
                  code_guards=[[0xf0000,0],[0xf0040,1],[0xfffc0,2]],crt_guards=[[0x400,0],[0x440,1],[0xfc0,2]])
    if result['objects_count']!=53:raise ValueError('object plan exceeds frozen count')
    return result

def guard_byte(region: int,slot: int,index: int,j: int) -> int:
    _integer(region,1,9);_integer(index,0,2);_integer(j,0,63)
    slots=64 if region==8 else (16 if region in (3,4,5) else 1)
    _integer(slot,0,slots-1)
    return (0xa5^region^slot^(index*0x3d)^j)&255

def initial_image(region: int,slot: int,payload_bytes: int=0) -> bytes:
    guard_byte(region,slot,0,0)
    sizes={1:0x100000,2:4096,3:0x20000,4:0x20000,5:4096,6:4096,7:4096,8:8192,9:4096}
    size=sizes[region];data=bytearray([0x6d])*size
    _integer(payload_bytes,0,size-192)
    if region in (3,4):
        positions=[(0,0),(64+payload_bytes,1),(size-64,2)]
        data[64:64+payload_bytes]=bytes(payload_bytes)
    elif region==1:
        positions=[(0xf0000,0),(0xf0040,1),(0xfffc0,2)]
        data[:0xf0000]=bytes(0xf0000)
    elif region==9:
        positions=[(0x400,0),(0x440,1),(0xfc0,2)];data[:0x400]=bytes(0x400)
    elif region==8:positions=[(0,0)]
    else:
        positions=[(0,0),(size-64,1)]
        if region==2:
            data[64:192]=bytes(128);positions=[(0,0),(192,1),(4032,2)]
        elif region==5:
            for offset,length in ((64,384),(1216,512),(2304,384),(2688,512)):data[offset:offset+length]=b'\xcc'*length
            for offset,length in ((448,512),(960,256),(1728,512)):data[offset:offset+length]=bytes(length)
        elif region==6:data[64:2304]=bytes(2240)
        elif region==7:data[64:320]=bytes(256)
    for offset,index in positions:data[offset:offset+64]=bytes(guard_byte(region,slot,index,j) for j in range(64))
    return bytes(data)

def verify_only_changes(initial: bytes,actual: bytes,mutable_ranges: list) -> bool:
    if len(initial)!=len(actual):return False
    cursor=0
    for offset,length in sorted(mutable_ranges):
        _integer(offset,0,len(initial));_integer(length,1,len(initial))
        if offset<cursor or offset+length>len(initial):raise ValueError('overlapping or invalid mutable ranges')
        if initial[cursor:offset]!=actual[cursor:offset]:return False
        cursor=offset+length
    return initial[cursor:]==actual[cursor:]

"""Administrator-side LE catalog encoder. This is never an ioctl trust input."""
import hashlib,struct,json,pathlib,sys
def build_catalog(entries):
    if not 1<=len(entries)<=16:raise ValueError('catalog count 1..16 required')
    records=[];schemas=[];offset=64+512*len(entries);ids=set()
    for item in entries:
        fields=list(item['fields']);digests=list(item['digests']);schema=item['schema']
        if len(fields)!=16 or any(type(n) is not int or not 0<=n<=0xffffffff for n in fields):raise ValueError('16 u32 fields required')
        if len(digests)!=8 or any(type(b) is not bytes or len(b)!=32 for b in digests):raise ValueError('eight SHA256 digests required')
        if type(schema) is not bytes or len(schema)<64 or len(schema)>65536 or len(schema)%64:raise ValueError('bounded binary schema required')
        header=struct.unpack('<16I',schema[:64])
        if header[0]!=0x534c5856 or header[1] not in (1,2) or header[2]!=len(schema) or not 1<=header[4]<=64 or len(schema)!=64+64*header[4] or any(header[12:]):raise ValueError('invalid schema header')
        if not fields[0] or not fields[1] or not fields[2] or fields[3] or fields[15] or not fields[5] or fields[5]%4 or fields[5]+fields[7]>8388608:raise ValueError('invalid program fields')
        if fields[4]+fields[5]>0xffffffff or fields[6]+fields[7]>0xffffffff:raise ValueError('container offset overflow')
        if fields[10]!=header[3] or not 0<fields[10]<=4096:raise ValueError('args schema mismatch')
        fields[11]=offset;fields[12]=len(schema);fields[13]=header[4]
        digests[2]=hashlib.sha256(schema).digest();digests[6]=bytes(32)
        record=struct.pack('<16I',*fields)+b''.join(digests)+bytes(192)
        eid=hashlib.sha256(record+schema).digest()
        if eid in ids:raise ValueError('duplicate catalog entry')
        ids.add(eid);record=record[:256]+eid+record[288:];records.append(record);schemas.append(schema);offset+=len(schema)
    if offset>65536:raise ValueError('catalog exceeds 64KiB')
    payload=b''.join(records+schemas)
    return struct.pack('<4I',0x434c5856,1,64+len(payload),len(entries))+hashlib.sha256(payload).digest()+struct.pack('<4I',512,0,0,0)+payload

def main():
    entries=json.loads(pathlib.Path(sys.argv[1]).read_text(encoding='utf-8'))['entries']
    for entry in entries:
        entry['schema']=bytes.fromhex(entry['schema_hex']);entry['digests']=[bytes.fromhex(x) for x in entry['digests']]
    raw=build_catalog(entries);out=pathlib.Path(sys.argv[2]);out.parent.mkdir(parents=True,exist_ok=True)
    with out.open('xb') as f:f.write(raw)
    print(json.dumps({'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest(),'scope':'CATALOG_ONLY_NOT_KERNEL_OR_BOARD','gpu_raw_cycles':None}))
if __name__=='__main__':main()

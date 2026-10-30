"""Bounded ELF structure inspection; not a compiler, execution or board receipt."""
import pathlib,hashlib,re,struct,stat
FORBIDDEN_RUNTIME_SYMBOLS=('s8_owner_bridge_create','image_owner_bridge_create','p8_owner_bridge_create','fake_s8_init','__wrap_ioctl')

def audit_production_symbols(inspection):
 required={'scope','target','machine','class_bits','test_bridge_symbols','target_executed','gpu_raw_cycles'}
 if not required<=set(inspection) or inspection['scope']!='STRUCTURAL_TARGET_ELF_ONLY_NOT_COMPILE_OR_EXECUTION_RECEIPT' or inspection['target']!='aarch64' or inspection['machine']!=183 or inspection['class_bits']!=64 or inspection['target_executed'] is not False or inspection['gpu_raw_cycles'] is not None:raise ValueError('complete structural AArch64 evidence required')
 if type(inspection['test_bridge_symbols']) is not list or inspection['test_bridge_symbols']:raise ValueError('test device bridge linked into production runtime')
 return None

def inspect_elf(path,target,expected_sha256):
 def require(condition,message):
  if not condition:raise ValueError(message)
 require(target in ('rv32','aarch64'),'explicit target required')
 require(isinstance(expected_sha256,str) and re.fullmatch('[0-9a-f]{64}',expected_sha256),'exact digest required')
 path=pathlib.Path(path);state=path.lstat();require(stat.S_ISREG(state.st_mode) and 52<=state.st_size<=256*1024*1024,'bounded regular target ELF required');raw=path.read_bytes()
 require(hashlib.sha256(raw).hexdigest()==expected_sha256,'target artifact digest changed')
 require(raw[:4]==b'\x7fELF' and raw[5:7]==b'\x01\x01','little-endian ELF version1 required')
 bits,machine=(32,243) if target=='rv32' else (64,183);require(raw[4]==(1 if bits==32 else 2),'ELF class/target mismatch')
 def take(offset,length):
  require(0<=offset<=len(raw) and 0<=length<=len(raw)-offset,'ELF range outside file');return raw[offset:offset+length]
 def unpack(fmt,offset):return struct.unpack(fmt,take(offset,struct.calcsize(fmt)))
 header=unpack('<HHIIIIIHHHHHH' if bits==32 else '<HHIQQQIHHHHHH',16)
 kind,actual,version,entry,phoff,shoff,flags,ehsize,phsize,phnum,shsize,shnum,shstrings=header
 require(actual==machine and version==1 and kind in ((2,) if bits==32 else (2,3)),'wrong target machine/version/executable type')
 require(ehsize==(52 if bits==32 else 64) and phsize==(32 if bits==32 else 56) and shsize==(40 if bits==32 else 64),'ELF table entry layout mismatch')
 require(0<phnum<=1024 and 0<shnum<=65535 and shstrings<shnum,'bounded complete program and section tables required');take(phoff,phnum*phsize);take(shoff,shnum*shsize)
 require((flags&7)==2 if bits==32 else flags==0,'target floating ABI/flags mismatch')
 loads=[];interpreters=[]
 for i in range(phnum):
  fields=unpack('<8I' if bits==32 else '<II6Q',phoff+i*phsize)
  if bits==32:ptype,offset,va,pa,length,memory,pflags,alignment=fields
  else:ptype,pflags,offset,va,pa,length,memory,alignment=fields
  if length:take(offset,length)
  if ptype==1:
   require(0< memory and length<=memory and va+memory<=1<<bits,'load extent mismatch')
   require(alignment in (0,1) or (alignment&(alignment-1)==0 and offset%alignment==va%alignment),'load alignment mismatch')
   loads.append(dict(start=va,end=va+memory,offset=offset,file_bytes=length,memory_bytes=memory,flags=pflags))
  if ptype==3:
   value=take(offset,length);require(1<len(value)<=256 and value[-1:]==b'\0' and b'\0' not in value[:-1],'interpreter must be one bounded string')
   try:interpreters.append(value[:-1].decode('ascii'))
   except UnicodeDecodeError:raise ValueError('non-ASCII interpreter')
 require(loads and any(p['flags']&1 and p['start']<=entry<p['end'] for p in loads),'entry outside executable load')
 require(interpreters==([] if bits==32 else ['/lib/ld-linux-aarch64.so.1']),'host or unexpected target interpreter')
 sections=[]
 for i in range(shnum):
  section=unpack('<10I' if bits==32 else '<II4QII2Q',shoff+i*shsize);name,stype,sflags,address,offset,length,link,info,alignment,entsize=section
  if stype!=8:take(offset,length)
  sections.append(dict(type=stype,offset=offset,bytes=length,link=link,entry_bytes=entsize))
 def string(section,index):
  require(section['type']==3 and 0<=index<section['bytes'],'string table/index mismatch');values=take(section['offset'],section['bytes']);end=values.find(b'\0',index);require(end>=index,'unterminated ELF string')
  try:return values[index:end].decode('utf8')
  except UnicodeDecodeError:raise ValueError('invalid ELF string encoding')
 needed=[];undefined=[];bridges=[]
 for section in sections:
  stype=section['type']
  if stype not in (2,6,11):continue
  require(section['link']<len(sections),'section link outside table');strings=sections[section['link']]
  size=(8 if bits==32 else 16) if stype==6 else (16 if bits==32 else 24)
  require(section['entry_bytes']==size and section['bytes']%size==0,'symbol/dynamic entry layout mismatch')
  ended=False
  for off in range(section['offset'],section['offset']+section['bytes'],size):
   if stype==6:
    tag,value=unpack('<iI' if bits==32 else '<qQ',off)
    if tag==0:ended=True;break
    require(tag not in (15,29),'RPATH/RUNPATH prohibited in target output')
    if tag==1:
     name=string(strings,value);require(name and '/' not in name and '\\' not in name and name not in ('.','..'),'target dependency must be a basename');needed.append(name)
   else:
    symbol=unpack('<IIIBBH' if bits==32 else '<IBBHQQ',off);index=symbol[0];shndx=symbol[5] if bits==32 else symbol[3]
    if index:
     name=string(strings,index)
     if shndx!=0 and name in FORBIDDEN_RUNTIME_SYMBOLS:bridges.append(name)
     if shndx==0 and name:
      require(not name.startswith(('Xil_','XTime_')),'baremetal undefined symbol in Linux target');undefined.append(name)
  if stype==6:require(ended,'dynamic table missing terminator')
 require(not needed if bits==32 else bool(needed),'target dependency model mismatch')
 require(bits!=32 or not undefined,'unresolved RV32 target symbol')
 after=path.lstat();require((after.st_ino,after.st_size,after.st_mtime_ns)==(state.st_ino,state.st_size,state.st_mtime_ns),'ELF changed while inspected')
 return dict(scope='STRUCTURAL_TARGET_ELF_ONLY_NOT_COMPILE_OR_EXECUTION_RECEIPT',target=target,class_bits=bits,machine=actual,entry=entry,type=kind,flags=flags,loads=loads,interpreter=interpreters[0] if interpreters else None,needed=sorted(set(needed)),undefined_symbol_count=len(set(undefined)),test_bridge_symbols=sorted(set(bridges)),bytes=len(raw),sha256=expected_sha256,target_executed=False,gpu_raw_cycles=None)

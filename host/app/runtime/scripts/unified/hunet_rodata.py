"""Fail-closed placement of two frozen expf constant-pool words in CODE.

The original toolchain object is never edited. Inspection067 established that
the only references to these words are two AUIPC/FLW pairs, with a0 overwritten
by the following compare before any call/store/address escape. Whole-object
identity is frozen; an unknown libc must be investigated, never auto-relabelled.
"""
import hashlib,struct
FROZEN='b3d2c0e07fa12b339b7772d22b0a7576e7cc630fcb050f84ca5b6a3de4c08923'
CONSTANTS=bytes.fromhex('1772b142b4f1cfc2')
def section_rows(b):
 if b[:7]!=b'\x7fELF\x01\x01\x01':raise ValueError('EXPF_ELF32')
 h=struct.unpack_from('<HHIIIIIHHHHHH',b,16)
 if h[0]!=1 or h[1]!=243 or h[10]!=40 or h[6]&7!=2:raise ValueError('EXPF_REL_RV32_SINGLE_ABI')
 rows=[struct.unpack_from('<10I',b,h[5]+i*40) for i in range(h[11])];s=rows[h[12]];names=b[s[4]:s[4]+s[5]]
 return {names[r[0]:].split(b'\0',1)[0].decode():dict(index=i,row=r,data=b[r[4]:r[4]+r[5]]) for i,r in enumerate(rows)}
def frozen(b):
 if hashlib.sha256(b).hexdigest()!=FROZEN:raise ValueError('EXPF_FROZEN_OBJECT_IDENTITY')
 rows=section_rows(b)
 if rows['.sdata']['data']!=CONSTANTS or rows['.sdata']['row'][2]!=3:raise ValueError('EXPF_CONSTANT_POOL')
 text=rows['.text.expf']['data']
 # These paired instructions load .LCPI0_0/.LCPI0_1 only. The relocation
 # symbol bindings and every other instruction are fixed by whole SHA above.
 for hi,lo,overwrite in ((0xbc,0xc0,0xc8),(0xdc,0xe0,0xe4)):
  a,l,z=[struct.unpack_from('<I',text,i)[0] for i in (hi,lo,overwrite)]
  if a&0xfff!=0x517 or l&0xfffff!=0x52787 or z&0xfff!=0x553:raise ValueError('EXPF_CONSTANT_REFERENCE_NOT_LOAD_ONLY')
 return rows
def check_derived(original,derived):
 old=frozen(original);new=section_rows(derived)
 if '.sdata' in new or new['.rodata.vxnn.expf.constants']['data']!=CONSTANTS or new['.rodata.vxnn.expf.constants']['row'][2]!=2:raise ValueError('EXPF_DERIVED_CONSTANT_NOT_READONLY')
 for name in ('.text.expf','.rela.text.expf','.riscv.attributes'):
  if old[name]['data']!=new[name]['data']:raise ValueError('EXPF_DERIVED_FUNCTION_RELOC_OR_SYMBOL_CHANGED '+name)
 # objcopy drops the empty .text section and renumbers subsequent section
 # indices. Compare every symbol's full semantics, including ordered symbol
 # positions (relocations retain their original symbol indices), not raw Ndx.
 def symbols(rows):
  table=rows['.symtab'];strings=next(s['data'] for s in rows.values() if s['index']==table['row'][6]);result=[]
  for i in range(0,len(table['data']),16):
   name,value,size,info,other,index=struct.unpack_from('<IIIBBH',table['data'],i);name=strings[name:].split(b'\0',1)[0].decode()
   section=next((n for n,r in rows.items() if r['index']==index),index)
   if section=='.sdata':section='.rodata.vxnn.expf.constants'
   result.append((name,value,size,info,other,section))
  return result
 if symbols(old)!=symbols(new):raise ValueError('EXPF_DERIVED_SYMBOL_BINDING_CHANGED')
 return dict(original_sha256=FROZEN,derived_sha256=hashlib.sha256(derived).hexdigest(),constant_sha256=hashlib.sha256(CONSTANTS).hexdigest(),constant_bytes=8,only_placement_metadata_changed=True,source_instructions_and_relocations_identical=True)

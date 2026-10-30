import hashlib,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def arr(b):return '{'+','.join('0x%02x'%v for v in b)+'}'
def generate(root=ROOT):
 m=json.loads((root/'contracts/hunet_v5.json').read_text(encoding='utf-8'));inputs=json.loads((root/'contracts/hunet_inputs.json').read_text(encoding='utf-8'))['records']
 lines=['#ifndef VXL_HUNET_ASSETS_H','#define VXL_HUNET_ASSETS_H',
        'static const vxl_u8 vxl_hunet_input_sha[127][32] __attribute__((unused))={']
 lines += [arr(bytes.fromhex(i['sha256']))+',' if i['included'] else '{0},' for i in inputs];lines+=['};']
 roles={'weights':'weights','jobs':'jobs_file','aux':'aux_file','model':'onnx'}
 for name,role in roles.items():
  record=next(x for x in m['files'] if x['role']==role)
  lines.append('static const vxl_u8 vxl_hunet_%s_sha[32] __attribute__((unused))=%s;'%(name,arr(bytes.fromhex(record['sha256']))))
 b=(root/'contracts/profiles/hw4-hunet-v5.json').read_bytes()
 lines += ['static const vxl_u8 vxl_hunet_profile_sha[32] __attribute__((unused))=%s;'%arr(hashlib.sha256(b).digest()),'#endif']
 (root/'generated/hunet/assets.h').write_text('\n'.join(lines)+'\n',encoding='utf-8',newline='\n')
if __name__=='__main__':generate()

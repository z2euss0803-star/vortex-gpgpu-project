"""Validate additive native provenance without promoting host/file evidence."""
import hashlib,re

def validate_native_origin(meta):
 if 'native_origin' not in meta:return None
 n=meta['native_origin']
 digests=('hardware','bit','xsa','compatible','model','plan','input','source','program','implementation','tensor')
 fields=set(digests)|set(('format','evidence_scope','boot_id','client_id','request_id','device_generation','provider_flags','input_bytes','raw_legacy_json','raw_legacy_sha256','quant_binding_format','layout_binding_format','numeric_class','build_id'))
 if not isinstance(n,dict) or set(n)!=fields:raise ValueError('complete exact native origin required')
 if n['format']!='unified-native-mosaic-origin-v1':raise ValueError('unknown native origin format')
 for key in digests+('raw_legacy_sha256',):
  if not isinstance(n[key],str) or not re.fullmatch('[0-9a-f]{64}',n[key]) or n[key]=='0'*64:raise ValueError('native identity digest')
 for key in ('boot_id','client_id','request_id'):
  if not isinstance(n[key],str) or not re.fullmatch('[A-Za-z0-9_.:-]{1,128}',n[key]):raise ValueError('native producer identity')
 if type(n['device_generation']) is not int or not 0<n['device_generation']<=0xffffffff or n['device_generation']!=meta['generation'] or n['provider_flags'] not in (1,4) or type(n['provider_flags']) is not int:raise ValueError('native source epoch/provider')
 if n['provider_flags']==4:
  expected=dict(hardware='1bf11d26789e7c602f9c05a5ecc97e2605ac7807deade8f4bfc549487f45ec5d',bit='94ae9d3e6eb73ba55a8eec8c802e3818cd6b3a9be7e60542da5ae9459e3579e1',xsa='02e68160e21b01621fde1eac6e472810e0fe50cd1842acb3ab63469314355e94',compatible='f670c653a37361f05f57c14eecd53dea1b87556489a2c1314efd208d715826dc')
  if any(n[k]!=v for k,v in expected.items()):raise ValueError('exact versioned S8 joint native source identity required')
 if type(n['input_bytes']) is not int or not 0<n['input_bytes']<=64*1024*1024:raise ValueError('native input extent')
 if n['boot_id']!=meta['boot_id'] or n['build_id']!=meta['build_id'] or n['numeric_class']!=meta['numeric_class']:raise ValueError('native source text differs')
 scope=n['evidence_scope']
 if scope not in ('OFFLINE','HOST_PROTOCOL_TEST','REAL_BOARD') or meta['scope']!=('REAL_BOARD' if scope=='REAL_BOARD' else 'OFFLINE'):raise ValueError('native source scope projection differs')
 if scope!='REAL_BOARD' and (meta['cycle_valid'] or meta['raw_cycles'] is not None):raise ValueError('host source cannot claim GPU cycles')
 if scope=='REAL_BOARD' and (not meta['cycle_valid'] or not meta['raw_cycles']):raise ValueError('original GPU cycles required')
 sha=lambda b:hashlib.sha256(b).hexdigest()
 if meta['run_id']!=sha(('\0'.join(n[k] for k in ('boot_id','client_id','request_id'))).encode())[:32]:raise ValueError('native request/result ID differs')
 for key in ('hardware','source','program','model','input'):
  if meta['identities'][key]!=n[key]:raise ValueError('native and envelope identity differ')
 if n['quant_binding_format']!='NATIVE_MOSAIC_QUANT_BINDING_V1' or meta['identities']['quant']!=sha(b'NATIVE_MOSAIC_QUANT_BINDING_V1\0'+bytes.fromhex(n['implementation'])+bytes.fromhex(n['model'])):raise ValueError('native quantization implementation/model binding differs')
 h,w=meta['shape'][2:]
 if n['layout_binding_format']!='NATIVE_CHW_F32_LE_LAYOUT_V1' or meta['identities']['layout']!=sha(('NATIVE_CHW_F32_LE_LAYOUT_V1:%d:%d'%(h,w)).encode()):raise ValueError('native layout binding differs')
 outputs=[x for x in meta['artifacts'] if x['role']=='output']
 if len(outputs)!=1 or outputs[0]['sha256']!=n['tensor']:raise ValueError('native original tensor differs')
 raw=n['raw_legacy_json']
 if not isinstance(raw,str) or not raw or len(raw.encode())>256*1024 or sha(raw.encode())!=n['raw_legacy_sha256']:raise ValueError('native original receipt differs')
 return n

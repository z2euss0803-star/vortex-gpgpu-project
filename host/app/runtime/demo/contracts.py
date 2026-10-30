"""Version 1 shared contracts. No implicit defaults for identity/validity."""
import copy
import json
import re

MAX_JSON = 256*1024
MAX_ASSET = 64*1024*1024
APPS = ('mosaic', 'hunet')
STATES = ('STAGED','LOADED','RUNNING','COMPUTE_DONE','RESULT_VALIDATED',
          'FAILED','QUARANTINED')
NUMERIC = ('NOT_EVALUATED','PASS','KNOWN_NUMERIC_DEVIATION','FAIL')
IDENTITIES = ('hardware','source','program','model','input','quant','layout')


def integer(value, maximum=(1 << 64)-1, minimum=0):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError('integer outside contract')
    return value


def hex_id(value, count):
    if not isinstance(value, str) or re.fullmatch('[0-9a-f]{%d}' % count, value) is None:
        raise ValueError('invalid identity')
    return value


def text(value, limit=128):
    if not isinstance(value, str) or not value or len(value.encode('utf-8')) > limit or any(ord(c)<32 for c in value):
        raise ValueError('invalid text')
    return value


def relative_name(value):
    text(value, 240)
    if '\\' in value or ':' in value or value.startswith('/') or any(p in ('','.', '..') for p in value.split('/')):
        raise ValueError('unsafe relative name')
    return value


def base(obj, required):
    if not isinstance(obj, dict) or any(key not in obj for key in required):
        raise ValueError('missing object or required field')
    try:
        encoded=json.dumps(obj,ensure_ascii=False,allow_nan=False).encode('utf-8')
    except (TypeError,ValueError): raise ValueError('not finite JSON')
    if len(encoded)>MAX_JSON:raise ValueError('request too large')


def validate_asset(obj):
    base(obj, ('role','bytes','sha256','name','shape','dtype'))
    if obj['role'] not in ('input','model','quant','glyph','display','output'):raise ValueError('asset role')
    relative_name(obj['name']);hex_id(obj['sha256'],64)
    size=integer(obj['bytes'],MAX_ASSET)
    shape=obj['shape'];dtype=obj['dtype']
    widths={'FP32':4,'I16':2,'U8':1,'RGB8':3,'BYTES':1}
    if dtype not in widths or not isinstance(shape,list) or not 1<=len(shape)<=4:raise ValueError('asset shape/dtype')
    product=widths[dtype]
    for dimension in shape:
        product*=integer(dimension,MAX_ASSET,1)
        if product>MAX_ASSET:raise ValueError('asset shape overflow')
    if product!=size:raise ValueError('asset shape bytes mismatch')
    return copy.deepcopy(obj)


def validate_frame(obj):
    base(obj,('schema','run_id','app','source_result_sha','width','height','stride',
              'format','bytes','sha256','layout','numeric_class'))
    if type(obj['schema']) is not int or obj['schema']!=1 or obj['app'] not in APPS:raise ValueError('frame schema/app')
    hex_id(obj['run_id'],32);hex_id(obj['source_result_sha'],64);hex_id(obj['sha256'],64)
    width=integer(obj['width'],1920,1);height=integer(obj['height'],1080,1)
    stride=integer(obj['stride'],MAX_ASSET,width*3)
    size=integer(obj['bytes'],MAX_ASSET)
    if size!=height*stride or obj['format']!='RGB8' or obj['layout']!='HWC_RGB8' or obj['numeric_class'] not in NUMERIC:
        raise ValueError('frame layout or size')
    return copy.deepcopy(obj)


def validate_run(obj):
    base(obj,('schema','run_id','app','boot_id','generation','build_id','scope','diagnostics',
              'shape','state','numeric_class','cycle_valid','timing_valid','raw_cycles','artifacts','identities'))
    if type(obj['schema']) is not int or obj['schema']!=1 or obj['app'] not in APPS:raise ValueError('run schema/app')
    hex_id(obj['run_id'],32);integer(obj['generation'],(1<<32)-1,1)
    text(obj['boot_id']);text(obj['build_id'])
    if any(type(obj[x]) is not bool for x in ('diagnostics','cycle_valid','timing_valid')):raise ValueError('explicit boolean required')
    if obj['scope'] not in ('OFFLINE','MOCK','REAL_BOARD'):raise ValueError('unknown evidence scope')
    if obj['scope']!='REAL_BOARD':
        if obj['raw_cycles'] is not None or obj['cycle_valid']:raise ValueError('offline cycles forbidden')
    else:
        if obj['boot_id'] in ('OFFLINE','MOCK'):raise ValueError('real boot identity required')
        if obj['cycle_valid']:integer(obj['raw_cycles'])
        elif obj['raw_cycles'] is not None:raise ValueError('invalid cycles must be null')
    if obj['state'] not in STATES or obj['numeric_class'] not in NUMERIC:raise ValueError('state/numeric classification')
    shape=obj['shape']
    if not isinstance(shape,list) or not 1<=len(shape)<=4:raise ValueError('shape')
    product=1
    for n in shape:
        product*=integer(n,(1<<31)-1,1)
        if product>(1<<64)-1:raise ValueError('shape overflow')
    ids=obj['identities']
    if not isinstance(ids,dict) or set(ids)!=set(IDENTITIES):raise ValueError('incomplete identities')
    for v in ids.values():hex_id(v,64)
    if not isinstance(obj['artifacts'],list) or len(obj['artifacts'])>32:raise ValueError('artifacts')
    for asset in obj['artifacts']:validate_asset(asset)
    timings=obj.get('timings_ns',{})
    if not isinstance(timings,dict) or len(timings)>64:raise ValueError('timings')
    for name,ns in timings.items():text(name,64);integer(ns)
    return copy.deepcopy(obj)

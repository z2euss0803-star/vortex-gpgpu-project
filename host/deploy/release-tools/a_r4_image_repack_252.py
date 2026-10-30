"""A-only offline container preparation. No target execution or media APIs.

Only new children of evidence/a-r4-image-repack-252 may be written. All APIs
operate on byte strings; no rootfs archive member is extracted to the host.
Container output never carries a release-ready or a boot/application claim.
"""
import argparse
import copy
import datetime
import gzip
import hashlib
import io
import json
import pathlib
import re
import shutil
import stat
import struct
import sys
import tarfile
import traceback
import zlib
sys.dont_write_bytecode = True
ORCH = pathlib.Path(__file__).resolve().parent
BASE = ORCH/'evidence/a-r4-image-repack-252'
RELEASE = ORCH/'delivery/release-r3-178'
ROOT = ORCH.parent/'vortex_linux_s8_perf_20261027'
MAX_FIT = 512*1024**2
MAX_CPIO = 1024**3
sys.path.insert(0,str(ROOT))
from scripts.unified.audit_kernel import read_fdt, audit_device_tree
from scripts.unified.audit_image import fit_payloads, normalize_cpio_entries, payload_errors, client_runtime_errors
from scripts.unified.bind_kernel import builtin_makefile, catalog_entries, firmware_inputs
sys.path.insert(0,str(ROOT/'scripts/cnn'))
import image_audit as _cpio
cpio_entries = _cpio.cpio_entries

def need(ok, reason):
    if not ok:raise ValueError(reason)

def digest(raw):return hashlib.sha256(raw).hexdigest()
def wire(value):return (json.dumps(value,sort_keys=True,indent=2)+'\n').encode()
def evidence(raw):return dict(bytes=len(raw),sha256=digest(raw))

def read_regular(path, maximum=MAX_FIT, *, allow_empty=False):
    path=pathlib.Path(path)
    before=path.lstat()
    need(stat.S_ISREG(before.st_mode) and (0<=before.st_size if allow_empty else 0<before.st_size) and before.st_size<=maximum,'bounded regular input required: '+str(path))
    raw=path.read_bytes();after=path.lstat()
    need((before.st_dev,before.st_ino,before.st_size,before.st_mtime_ns)==(after.st_dev,after.st_ino,after.st_size,after.st_mtime_ns),'input changed while read')
    need(len(raw)==before.st_size,'incomplete input read')
    return raw

def read_ref(row, maximum=MAX_FIT):
    need(isinstance(row,dict) and set(row)=={'path','bytes','sha256'},'exact path/bytes/sha256 input reference required')
    need(type(row['bytes']) is int and 0<row['bytes']<=maximum and isinstance(row['sha256'],str) and re.fullmatch('[0-9a-f]{64}',row['sha256']),'typed bounded input identity')
    raw=read_regular(row['path'],maximum)
    need(evidence(raw)=={k:row[k] for k in ('bytes','sha256')},'pinned input identity differs: '+row['path'])
    return raw

def new_output(path):
    # Check before mkdir. Existing runs, the base itself and all other paths fail.
    supplied=pathlib.Path(path).absolute()
    need(not any(p.is_symlink() for p in (supplied,*supplied.parents)),'output symlink/reparse path forbidden')
    path=supplied.resolve();base=BASE.resolve()
    need(base in path.parents and path!=base,'output must be a new child of the252 evidence directory')
    path.mkdir(parents=True,exist_ok=False)
    return path

def expand_gzip(raw, limit):
    need(isinstance(raw,bytes) and raw.startswith(b'\x1f\x8b\x08') and type(limit) is int and 0<limit<=MAX_CPIO,'bounded gzip stream required')
    d=zlib.decompressobj(31)
    try:expanded=d.decompress(raw,limit+1)
    except zlib.error as error:raise ValueError('invalid gzip stream') from error
    need(len(expanded)<=limit and d.eof and not d.unused_data and not d.unconsumed_tail,'gzip exceeds bound, is incomplete, or contains concatenated/trailing data')
    return expanded

def gzip_bytes(raw):
    need(isinstance(raw,bytes) and len(raw)<=MAX_CPIO,'bounded gzip source')
    out=io.BytesIO()
    with gzip.GzipFile(filename='',mode='wb',fileobj=out,mtime=0,compresslevel=9) as f:f.write(raw)
    return out.getvalue()

def fdt_locations(raw):
    nodes=read_fdt(raw,max_bytes=MAX_FIT)
    h=list(struct.unpack_from('>10I',raw));so,st,ss= h[2],h[3],h[8]
    end=so+h[9]
    need(40<=h[4]<so<=end<=st<=st+ss<=len(raw),'canonical ordered FDT blocks required')
    names=raw[st:st+ss];p=so;stack=[];locations={}
    while p<end:
        start=p;token=struct.unpack_from('>I',raw,p)[0];p+=4
        if token==1:
            stop=raw.index(0,p,end);name=raw[p:stop].decode('ascii');p=(stop+4)&~3
            stack.append('/' if not stack else stack[-1].rstrip('/')+'/'+name)
        elif token==2:stack.pop()
        elif token==3:
            size,offset=struct.unpack_from('>II',raw,p);p+=8
            stop=names.index(0,offset);name=names[offset:stop].decode('ascii')
            value=p;p=(p+size+3)&~3
            locations[(stack[-1],name)]=(start,p,value,size,offset)
        elif token==4:pass
        elif token==9:break
        else:raise ValueError('unknown checked FDT token')
    return h,nodes,locations

def replace_fdt(raw, updates):
    """Preserve all original tokens/strings/reservations except named properties."""
    h,nodes,locations=fdt_locations(raw)
    need(isinstance(updates,dict) and set(updates)<=set(locations),'FDT replacement must name existing properties')
    need(all(isinstance(v,bytes) for v in updates.values()),'FDT replacement values must be bytes')
    wanted=copy.deepcopy(nodes)
    pieces=[];cursor=h[2]
    for key in sorted(updates,key=lambda k:locations[k][0]):
        first,last,value,size,offset=locations[key];data=updates[key]
        need(len(data)<=MAX_FIT,'FDT property length bound')
        wanted[key[0]][key[1]]=data;pieces.append(raw[cursor:first])
        if data==raw[value:value+size]:pieces.append(raw[first:last])
        else:pieces.append(struct.pack('>III',3,len(data),offset)+data+bytes((-len(data))%4))
        cursor=last
    old_end=h[2]+h[9];pieces.append(raw[cursor:old_end]);body=b''.join(pieces)
    delta=len(body)-h[9];prefix=bytearray(raw[:h[2]])
    h[1]+=delta;h[3]+=delta;h[9]=len(body)
    need(56<=h[1]<=MAX_FIT,'new FDT length bound')
    struct.pack_into('>10I',prefix,0,*h)
    result=bytes(prefix)+body+raw[old_end:]
    need(read_fdt(result,max_bytes=MAX_FIT)==wanted,'independent FDT property projection differs')
    return result

def newc_records(raw):
    """Return exact physical records after independent semantic validation."""
    need(isinstance(raw,bytes) and 0<len(raw)<=MAX_CPIO,'bounded newc bytes required')
    cpio_entries(raw)
    at=0;records=[];seen=set()
    while at<len(raw):
        start=at;header=raw[at:at+110];at+=110
        need(len(header)==110 and header[:6] in (b'070701',b'070702'),'complete newc header required')
        try:fields=[int(header[i:i+8],16) for i in range(6,110,8)]
        except ValueError as error:raise ValueError('newc hexadecimal field') from error
        size,namesize=fields[6],fields[11]
        need(1<=namesize<=4096 and at+namesize<=len(raw),'bounded newc name')
        encoded=raw[at:at+namesize];need(encoded[-1:]==b'\0' and b'\0' not in encoded[:-1],'terminated newc name')
        name=_cpio.normalized_name(encoded[:-1].decode('utf8'));name_end=at+namesize;at=(name_end+3)&~3
        need(at<=len(raw) and not any(raw[name_end:at]),'complete zero newc name padding')
        need(size<=len(raw)-at,'newc data range');data=raw[at:at+size];data_end=at+size;at=(data_end+3)&~3
        need(at<=len(raw) and not any(raw[data_end:at]),'complete zero newc data padding')
        if name=='TRAILER!!!':
            need(not size and not any(raw[at:]),'one final zero-padded cpio trailer')
            return records,raw[start:at]
        need(name not in seen and len(records)<32768,'bounded unique physical newc inventory');seen.add(name)
        records.append(dict(name=name,fields=fields,raw=raw[start:at],data=data))
    raise ValueError('missing physical newc trailer')

def member_name(name):
    need(isinstance(name,str) and name and '\0' not in name and '\\' not in name and all(ord(c)>=32 for c in name),'canonical archive member string')
    p=pathlib.PurePosixPath(name)
    need(not p.is_absolute() and str(p)==name and all(x not in ('','.','..') for x in name.split('/')),'unsafe archive member')
    return name

def newc_record(name,data,mode,kind,inode,epoch):
    encoded=member_name(name).encode()+b'\0'
    fields=[inode,kind|mode,0,0,2 if kind==stat.S_IFDIR else 1,epoch,len(data),0,0,0,0,len(encoded),0]
    need(all(type(n) is int and 0<=n<=0xffffffff for n in fields),'newc field exceeds32 bits')
    header=b'070701'+b''.join(('%08x'%n).encode() for n in fields)+encoded
    return header+bytes((-len(header))%4)+data+bytes((-len(data))%4)

def overlay_newc(raw, files, directories, removals, epoch):
    records,trailer=newc_records(raw);semantic=cpio_entries(raw);byname={r['name']:r for r in records}
    need(type(epoch) is int and 0<=epoch<=0xffffffff,'explicit uint32 archive epoch')
    need(isinstance(files,dict) and isinstance(directories,dict) and isinstance(removals,list) and len(removals)==len(set(removals)),'unique typed overlay declarations')
    names=set(files)|set(directories)|set(removals)
    for name in names:member_name(name)
    need(not (set(files)&set(directories) or set(removals)&(set(files)|set(directories))),'overlapping overlay ownership')
    for name in removals:
        need(name in byname,'only existing declared members can be removed')
        kind=stat.S_IFMT(byname[name]['fields'][1])
        need(kind==stat.S_IFDIR or (kind==stat.S_IFREG and byname[name]['fields'][4]==1),'only regular non-hardlink files or completely removed directories')
        if kind==stat.S_IFDIR:need(all(n in removals for n in byname if n.startswith(name+'/')),'removed directory has surviving descendants')
    for name,value in files.items():
        need(isinstance(value,tuple) and len(value)==2 and isinstance(value[0],bytes),'file bytes/mode pair required')
        mode=value[1];need(type(mode) is int and 0<=mode<=0o777 and not mode&0o022 and mode&0o400,'root-owned non-group/other-writable readable file mode')
        if name in byname:need(stat.S_IFMT(byname[name]['fields'][1])==stat.S_IFREG and byname[name]['fields'][4]==1,'file replacement must not alter a link or hardlink group')
    for name,mode in directories.items():
        need(type(mode) is int and 0<=mode<=0o777 and not mode&0o022 and mode&0o500==0o500,'safe searchable root directory mode')
        if name in semantic:need(semantic[name]['kind']==stat.S_IFDIR,'directory replacement type differs')
    for name in set(files)|set(directories):
        for parent in pathlib.PurePosixPath(name).parents:
            if str(parent)=='.':continue
            parent=str(parent)
            need(parent in directories or (parent in semantic and parent not in removals and semantic[parent]['kind']==stat.S_IFDIR and semantic[parent]['uid']==semantic[parent]['gid']==0 and not semantic[parent]['mode']&0o022),'missing, unsafe or symlink archive parent: '+parent)
    if not names:return raw
    inode=max((r['fields'][0] for r in records),default=0)+1;pieces=[]
    changed=set(files)|set(directories)
    for r in records:
        name=r['name']
        if name in removals:continue
        if name in changed:
            data,mode,kind=(*files[name],stat.S_IFREG) if name in files else (b'',directories[name],stat.S_IFDIR)
            pieces.append(newc_record(name,data,mode,kind,inode,epoch));inode+=1
        else:pieces.append(r['raw'])
    for name in sorted(set(directories)-set(byname),key=lambda n:(n.count('/'),n)):
        pieces.append(newc_record(name,b'',directories[name],stat.S_IFDIR,inode,epoch));inode+=1
    for name in sorted(set(files)-set(byname)):
        data,mode=files[name];pieces.append(newc_record(name,data,mode,stat.S_IFREG,inode,epoch));inode+=1
    result=b''.join(pieces)+trailer;result+=bytes((-len(result))%512)
    observed=cpio_entries(result);expected=copy.deepcopy(semantic)
    for name in removals:expected.pop(name)
    for name,(data,mode) in files.items():expected[name]=dict(kind=stat.S_IFREG,mode=mode,uid=0,gid=0,data=data,device=None)
    for name,mode in directories.items():expected[name]=dict(kind=stat.S_IFDIR,mode=mode,uid=0,gid=0,data=b'',device=None)
    need(observed==expected,'independent cpio projection changed outside declared overlay')
    newc_records(result)
    return result

def expand_prefix_removals(raw,prefixes,keep=()):
    """Remove old members under exact directory prefixes except explicit new names.

    Pass all new files AND directories as keep. A retained old directory is
    therefore explicit; stale old files/subdirectories are never inherited.
    overlay_newc separately refuses partially removed dirs and hardlinks.
    """
    entries=cpio_entries(raw);need(isinstance(prefixes,(list,tuple)) and len(prefixes)==len(set(prefixes)),'unique replacement prefixes')
    retained=set(keep)
    for name in retained:member_name(name)
    remove=set()
    for prefix in prefixes:
        member_name(prefix);need(entries.get(prefix,{}).get('kind')==stat.S_IFDIR,'existing exact directory replacement prefix required')
        remove.update(name for name in entries if name==prefix or name.startswith(prefix+'/'))
    return sorted(remove-retained)

def selected_fit(raw):
    payloads=fit_payloads(raw);nodes=read_fdt(raw,max_bytes=MAX_FIT)
    text=lambda v:v[:-1].decode('ascii')
    default=text(nodes['/configurations']['default']);config=nodes['/configurations/'+default]
    paths={role:'/images/'+text(config[role]) for role in ('kernel','fdt','ramdisk')}
    children={p for p in nodes if p.rsplit('/',1)[0]=='/images'}
    need(children==set(paths.values()),'exact three selected inline FIT images required')
    need({p for p in nodes if p.rsplit('/',1)[0]=='/configurations'}=={'/configurations/'+default},'exact one selected FIT configuration required')
    need(not any('signature' in p.rsplit('/',1)[-1] for p in nodes),'signed FIT is outside this unsigned container helper')
    hashes={}
    for role,path in paths.items():
        sub=[p for p in nodes if p.rsplit('/',1)[0]==path]
        need(len(sub)==1 and nodes[sub[0]].get('algo')==b'sha256\0','one SHA256 node per payload')
        hashes[role]=sub[0]
    return payloads,nodes,paths,hashes

def pack_fit(raw,dtb,rootfs_gzip,build_id,epoch):
    need(isinstance(build_id,str) and re.fullmatch('[A-Za-z0-9_.-]{1,128}',build_id),'explicit bounded new software build ID')
    need(type(epoch) is int and 0<=epoch<=0xffffffff,'explicit FIT epoch')
    old,nodes,paths,hashes=selected_fit(raw)
    read_fdt(dtb);newc_records(expand_gzip(rootfs_gzip,MAX_CPIO))
    need('description' in nodes['/'] and 'timestamp' in nodes['/'],'explicit original FIT description/timestamp')
    updates={('/','description'):build_id.encode()+b'\0',('/','timestamp'):struct.pack('>I',epoch)}
    for role,data in (('fdt',dtb),('ramdisk',rootfs_gzip)):
        updates[(paths[role],'data')]=data;updates[(hashes[role],'value')]=hashlib.sha256(data).digest()
    packed=replace_fdt(raw,updates);observed=fit_payloads(packed)
    need(observed==dict(kernel=old['kernel'],dtb=dtb,rootfs_gzip=rootfs_gzip),'independent final FIT extraction differs')
    need(0<len(packed)<0x20000000 and 0<len(old['kernel'])<0x10000000-0x80000,'original FIT/kernel load-window bounds')
    need(read_fdt(packed,max_bytes=MAX_FIT)[paths['kernel']]==nodes[paths['kernel']],'frozen compressed kernel or metadata changed')
    return packed

# Pinned delivered A-local R3 artifacts. Historical B paths inside receipts are
# evidence strings only: the helper never opens them or a remote connection.
PINS={
    'bundle-manifest.json':'ebaace1d3672bd6d1e120f5d8420532600d9856f6b38276f95b2e96f1c345631',
    'image-audit.json':'33d96887b95bfb409d6664f1ed3a678bc7123b32ab54f28ed9a2400084573987',
    'kernel-audit.json':'03436054f96bccfa71a3c374f5c88252c752236901ffc212896834b7bb24e298',
    'cross-audit.json':'b7b0402813db1b987c1311439df18aac61d70d959a4171389550ddf656ff821d',
    'BOOT.BIN':'ae3a3bb804f201a899d6bbaa7801312d980d82ae6e30049aa40b6f0d1a52a2d3',
    'boot.scr':'34f38b3e2d099205c7a12a0532bdba28c766843c481dae5336cbdf33ec24288e',
    'image.ub':'3de40c654123390a262e9c9c72834f5b114f103debd135a254771d2421e5c443',
}

def baseline():
    loaded={}
    for name,want in PINS.items():
        raw=read_regular(RELEASE/name)
        need(digest(raw)==want,'frozen R3 delivery changed: '+name);loaded[name]=raw
    manifest=json.loads(loaded['bundle-manifest.json']);audit=json.loads(loaded['image-audit.json']);kernel=json.loads(loaded['kernel-audit.json'])
    need(audit.get('natural_exit')==0 and audit.get('errors')==[] and audit['scope']=='ACTUAL_PROVIDER_COMPLETE_BOOT_FIT_ROOTFS_CONTENT_NOT_BOARD_OR_APPLICATION_PASS','original successful content audit required')
    need(kernel.get('natural_exit')==0 and kernel['provider']==audit['provider'],'original successful kernel and same provider required')
    for row in manifest['files']:
        need(row['name'] in loaded and evidence(loaded[row['name']])=={k:row[k] for k in ('bytes','sha256')},'original manifest boot-file binding')
    fit,nodes,paths,hashes=selected_fit(loaded['image.ub'])
    need({k:evidence(v) for k,v in fit.items()}==audit['fit_payloads'],'original independently extracted FIT component digests')
    dt=audit_device_tree(fit['dtb'],audit['provider'],kernel['firmware']['catalog-v1.bin']['sha256'],kernel['firmware']['execution-v1.bin']['sha256'])
    cpio=expand_gzip(fit['rootfs_gzip'],MAX_CPIO);records,trailer=newc_records(cpio);entries=cpio_entries(cpio)
    payload=json.loads(entries['usr/share/vxgpu/payload-manifest.json']['data'])
    errors=payload_errors(normalize_cpio_entries(entries),payload)
    need(not errors,'original payload admission differs: '+repr(errors))
    return dict(loaded=loaded,manifest=manifest,audit=audit,kernel=kernel,fit=fit,nodes=nodes,paths=paths,hashes=hashes,dt=dt,cpio=cpio,records=records,entries=entries)

def packet_comparison(kernel):
    present={};missing=[];matched=[];changed=[]
    legacy={('driver/'+k):v for k,v in kernel['legacy_headers'].items()}
    for row in kernel['kernel_input_files']:
        name=row['path'];member_name(name);path=ROOT/name
        if not path.is_file():
            need(name in legacy and all(legacy[name][k]==row[k] for k in ('bytes','sha256')),'unexplained missing kernel packet input: '+name)
            missing.append(dict(row,scope='FROZEN_IN_ORIGINAL_AUDITED_KERNEL_NOT_READ_AGAIN_ON_A'))
            continue
        raw=read_regular(path,64*1024**2,allow_empty=row['bytes']==0)
        if name=='driver/Makefile':raw=builtin_makefile(raw)
        present[name]=raw
        if evidence(raw)=={k:row[k] for k in ('bytes','sha256')}:matched.append(row)
        else:changed.append(dict(path=name,original={k:row[k] for k in ('bytes','sha256')},candidate=evidence(raw)))
    unused=[]
    for row in changed:
        name=row['path']
        # This one C++ user-program declaration was copied into the old source
        # packet but is outside the unchanged Linux C driver/contract. Do not
        # call the complete packet byte-identical. No general allowlist exists.
        need(name=='include/vxnn/mosaic_ro64_program.hpp','kernel packet input changed: '+name)
        raw=present[name];need(b'namespace vxnn' in raw and b'mosaic_ro64_code_sha256' in raw,'exact user declaration classification')
        references=[]
        for other,data in present.items():
            if other==name:continue
            for line in data.splitlines():
                if re.match(rb'\s*#\s*include\b',line) and b'mosaic_ro64_program' in line:references.append(other)
        need(not references,'changed user header referenced by original kernel packet')
        unused.append(dict(row,scope='EXPLICIT_USER_CPP_DECLARATION_PACKET_DIFFERENCE_NO_KERNEL_REBUILD',literal_include_references=references))
    contract=[r for r in matched if r['path'].startswith(('driver/','core/','include/vxl/','generated/'))]
    need(contract and not any(r['path'].startswith(('driver/','core/','include/vxl/','generated/')) for r in changed),'unchanged kernel C driver/contract inputs required')
    return dict(original_inputs=len(kernel['kernel_input_files']),available_byte_exact=len(matched),available_byte_exact_rows=matched,legacy_headers_retained=missing,user_cpp_packet_differences=unused,full_packet_byte_exact=not (missing or changed),kernel_C_driver_contract_byte_exact=True,kernel_C_driver_contract_files=len(contract))

def rebind_context(base,build_id,service_default):
    need(re.fullmatch('[A-Za-z0-9_.-]{1,128}',build_id or '') and build_id!=base['audit']['build_id'],'new bounded build ID must differ from frozen R3')
    need(type(service_default) is int and service_default==0,'first R4 retains the original disabled startup policy')
    comparison=packet_comparison(base['kernel']);fw=firmware_inputs(ROOT,4)
    cat,exe=fw['catalog'],fw['execution'];oldcat=base['entries']['lib/firmware/vxl/catalog-v1.bin']['data'];oldexe=base['entries']['lib/firmware/vxl/execution-v1.bin']['data']
    oldrows,newrows=catalog_entries(oldcat),catalog_entries(cat)
    need(len(oldrows)==len(newrows)==len(fw['entries'])==16,'exact joint16 firmware rows')
    need(cat!=oldcat and exe!=oldexe,'independent R4 firmware identities required')
    need(exe[:16]==oldexe[:16] and exe[48:64]==oldexe[48:64] and exe[16:48]==hashlib.sha256(cat).digest(),'exact execution header and new catalog digest')
    for i,(old,new,entry) in enumerate(zip(oldrows,newrows,fw['entries'])):
        need(old['fields']==new['fields'] and old['schema']==new['schema'],'catalog geometry/schema/UAPI changed')
        need(new['digests'][1:6]==old['digests'][1:6],'readonly/schema/hardware identity changed')
        if i<2:need(new['digests'][0]!=old['digests'][0] and new['digests'][7]==new['digests'][0],'both NN programs need new independent exact CODE identities')
        else:need(new['digests']==old['digests'],'frozen IMAGE/P8 shader identity changed')
        need(exe[64+256*i+32:64+256*(i+1)]==oldexe[64+256*i+32:64+256*(i+1)] and exe[64+256*i:96+256*i]==new['digests'][6],'execution watchdog/guard/retirement policy changed')
        need(entry['entry_id']==new['digests'][6].hex(),'new payload entry ID differs')
    backend=base['dt']['backend'];updates={(backend,'vortex,catalog-sha256'):hashlib.sha256(cat).digest(),(backend,'vortex,execution-sha256'):hashlib.sha256(exe).digest()}
    dt=replace_fdt(base['fit']['dtb'],updates);original=base['fit']['dtb'];expected=bytearray(original);_,_,locations=fdt_locations(original)
    for key,data in updates.items():
        start,end,value,size,offset=locations[key];need(size==len(data)==32,'fixed32 DT identity property required');expected[value:value+32]=data
    need(dt==bytes(expected),'DT changed outside the two exact32 byte windows')
    audited=audit_device_tree(dt,base['audit']['provider'],digest(cat),digest(exe))
    rec=dict(scope='SOFTWARE_ONLY_REUSED_KERNEL_NEW_DT_NOT_REBUILT',natural_exit=0,build_id=build_id,old_build_id=base['audit']['build_id'],provider=base['audit']['provider'],kernel_receipt=dict(path=str(RELEASE/'kernel-audit.json'),sha256=PINS['kernel-audit.json'],scope=base['kernel']['scope']),image_receipt=dict(path=str(RELEASE/'image-audit.json'),sha256=PINS['image-audit.json']),kernel_source_id=base['audit']['kernel_audit']['kernel_source_id'],kernel_image=base['audit']['fit_payloads']['kernel'],kernel_rebuilt=False,kernel_compile_repeated=False,kernel_packet_comparison=comparison,old_DT=base['audit']['fit_payloads']['dtb'],new_DT=evidence(dt),DT_changed_properties=[list(k) for k in updates],DT_only_fixed32_catalog_execution_changed=True,device_tree=audited,catalog=evidence(cat),execution=evidence(exe),execution_policy_tail_bytes_unchanged=True,frozen_BOOT=evidence(base['loaded']['BOOT.BIN']),frozen_boot_script=evidence(base['loaded']['boot.scr']),startup_policy=dict(previous_default=0,requested_default=service_default,scope='EXPLICIT_NEW_STARTUP_POLICY_REQUIRES_OWN_ADMISSION' if service_default else 'ORIGINAL_DISABLED_STARTUP_POLICY',required_gates=['DP_READY_BEFORE_SERVICE','NN_IDLE_SINGLE_GPU_OWNER'] if service_default else []),ready_for_sd=False,source_lock_and_cross_and_rootfs_audit_required=True,gpu_raw_cycles=None,board='NOT_RUN',sd='NOT_TOUCHED')
    return rec,dt,fw

def audit_rootfs(raw_cpio,build_id,cross_receipt=None,installed_models=None):
    """Re-audit actual archive bytes, captured links, packages, and target runtime.

    installed_models is {id: installed_model} from the genuine matching HOST
    preparation probe. Its equality is checked, never a declaration of GPU PASS.
    A missing cross/probe is a listed pending gate, not a fabricated acceptance.
    """
    newc_records(raw_cpio);entries=cpio_entries(raw_cpio);normalized=normalize_cpio_entries(entries)
    raw=lambda n:entries.get(n,{}).get('data',b'')
    manifest=json.loads(raw('usr/share/vxgpu/payload-manifest.json'));need(manifest['build_id']==build_id,'rootfs payload build ID differs')
    errors=payload_errors(normalized,manifest);need(not errors,'actual original payload admission: '+repr(errors))
    installation=json.loads(raw('etc/vxgpud/installation.json'));declared={r['id']:r for r in installation['nn']}
    need(set(declared)=={'hunet','mosaic'},'exact two NN installations')
    packages={}
    for name,row in declared.items():
        prefix='usr/share/vxgpu/models/'+name
        need(row['package']=='/'+prefix,'bounded exact installed model directory')
        package=json.loads(raw(prefix+'/manifest.json'));sealed=copy.deepcopy(package);model_id=sealed.pop('model_id')
        need(digest(json.dumps(sealed,sort_keys=True,separators=(',',':'),ensure_ascii=False).encode())==model_id==row['model'],'actual installed sealed model ID')
        names={prefix+'/manifest.json'}
        for asset in package['assets']:
            member_name(asset['path']);path=prefix+'/'+asset['path'];need(path not in names,'unique model asset path');names.add(path)
            need(evidence(raw(path))=={k:asset[k] for k in ('bytes','sha256')},'actual package asset differs: '+path)
        need({n for n,v in entries.items() if n.startswith(prefix+'/') and v['kind']!=stat.S_IFDIR}==names,'exact model member inventory; no stale/link/extra assets')
        packages[name]=dict(model=model_id,members=len(names),installed_model=row)
    sys.path.insert(0,str(ROOT/'scripts/hunet'))
    from scripts.hunet import image_audit as legacy
    resolved=legacy.resolved_regular_files(entries)
    errors=client_runtime_errors(resolved);need(not errors,'actual Python control-client/runtime closure: '+repr(errors))
    if cross_receipt is not None:
        from scripts.unified.cross_common import snapshot, aarch64_target
        from scripts.unified.bind_kernel import require_cross_inputs
        require_cross_inputs(cross_receipt,snapshot(ROOT),aarch64_target(4))
        for row in cross_receipt['target_runtime']['files'].values():
            data=resolved.get(row['path'],b'')
            need(digest(data)==row['sha256'] and data[:7]==b'\x7fELF\x02\x01\x01' and int.from_bytes(data[18:20],'little')==183,'actual matched AArch64 runtime closure: '+row['path'])
        for row in cross_receipt['installation']:
            need(row['destination'] in ('/usr/bin/vxgpud','/usr/bin/vxgpuctl'),'only production daemon/client install')
            data=resolved.get(row['destination'][1:],b'');need(digest(data)==row['sha256'],'actual installed cross artifact differs')
            if row['destination']=='/usr/bin/vxgpud':need(data[:7]==b'\x7fELF\x02\x01\x01' and int.from_bytes(data[18:20],'little')==183,'real production AArch64 daemon required')
    if installed_models is not None:need(installed_models==declared,'actual rootfs installation differs from matching HOST model admission')
    return dict(scope='ACTUAL_CPIO_PAYLOAD_MODEL_ASSETS_CLIENT_RUNTIME_BYTES_NOT_BOARD',build_id=build_id,payload_manifest=evidence(raw('usr/share/vxgpu/payload-manifest.json')),packages=packages,cross_receipt_checked=cross_receipt is not None,matching_HOST_model_admission_checked=installed_models is not None,pending_gates=(["MATCHING_AARCH64_CROSS_AND_SOURCE_LOCK"] if cross_receipt is None else [])+(["MATCHING_HOST_MODEL_GRAPH_PLAN_ADMISSION"] if installed_models is None else []),gpu_raw_cycles=None,ready_for_sd=False)

def tar_mirror(entries,path,epoch):
    with pathlib.Path(path).open('xb') as raw:
        with gzip.GzipFile(filename='',fileobj=raw,mode='wb',mtime=0,compresslevel=9) as gz:
            with tarfile.open(fileobj=gz,mode='w|',format=tarfile.PAX_FORMAT) as tar:
                for name,row in entries.items():
                    item=tarfile.TarInfo(name or '.');item.mode=row['mode'];item.uid=row['uid'];item.gid=row['gid'];item.mtime=epoch;kind=row['kind'];data=row['data']
                    if kind==stat.S_IFREG:item.type=tarfile.REGTYPE;item.size=len(data)
                    elif kind==stat.S_IFDIR:item.type=tarfile.DIRTYPE
                    elif kind==stat.S_IFLNK:item.type=tarfile.SYMTYPE;item.linkname=data.decode('utf8')
                    elif kind in (stat.S_IFCHR,stat.S_IFBLK):item.type=tarfile.CHRTYPE if kind==stat.S_IFCHR else tarfile.BLKTYPE;item.devmajor,item.devminor=row['device']
                    elif kind==stat.S_IFIFO:item.type=tarfile.FIFOTYPE
                    else:raise ValueError('unsupported tar mirror type')
                    tar.addfile(item,io.BytesIO(data) if kind==stat.S_IFREG else None)

def write_context(out,context,dt,fw):
    (out/'system.dtb').write_bytes(dt);folder=out/'firmware';(folder/'programs').mkdir(parents=True)
    artifacts={}
    for name,raw in [('catalog-v1.bin',fw['catalog']),('execution-v1.bin',fw['execution'])]+[('programs/'+e['entry_id']+'.bin',e['payload']) for e in fw['entries']]:
        path=folder/name;path.write_bytes(raw);artifacts[name]=dict(path=str(path),**evidence(raw))
    context['firmware']=artifacts;context['dtb']=dict(path=str(out/'system.dtb'),**evidence(dt));context['helper']=dict(path=str(pathlib.Path(__file__).resolve()),**evidence(pathlib.Path(__file__).read_bytes()))
    (out/'software-reuse-context.json').write_bytes(wire(context))

def main():
    parser=argparse.ArgumentParser(__doc__);parser.add_argument('mode',choices=('inspect','context','overlay'))
    parser.add_argument('--root',default=str(ROOT));parser.add_argument('--out',required=True)
    parser.add_argument('--build-id');parser.add_argument('--service-default',type=int,choices=(0,),default=0)
    parser.add_argument('--overlay-plan',help='pinned file table, explicit directory modes/removals; container preparation only')
    args=parser.parse_args();need(pathlib.Path(args.root).resolve()==ROOT.resolve(),'this252 helper is bound to the exact R4 source root')
    out=new_output(args.out);rec=dict(scope='A_LOCAL_252_OFFLINE_CONTAINER_PREPARATION_NOT_RELEASE_READY',natural_exit=1,ready_for_sd=False,mode=args.mode,started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),gpu_raw_cycles=None,board='NOT_RUN',sd='NOT_TOUCHED')
    try:
        base=baseline();rec['baseline']={name:dict(path=str(RELEASE/name),sha256=want) for name,want in PINS.items()}
        rec['original_components']={k:evidence(v) for k,v in base['fit'].items()};rec['original_records']=len(base['records']);rec['original_cpio']=evidence(base['cpio']);rec['original_hardlink_records']=sum(r['fields'][4]>1 and stat.S_IFMT(r['fields'][1])==stat.S_IFREG for r in base['records'])
        need(replace_fdt(base['loaded']['image.ub'],{})==base['loaded']['image.ub'],'actual FIT identity roundtrip')
        need(overlay_newc(base['cpio'],{}, {}, [],0)==base['cpio'],'actual physical cpio identity roundtrip')
        rec['actual_R3_identity_roundtrip']=True;rec['packet_comparison']=packet_comparison(base['kernel'])
        if args.mode=='inspect':
            inventory={name:dict(kind=row['kind'],mode=row['mode'],uid=row['uid'],gid=row['gid'],bytes=len(row['data']),sha256=digest(row['data']),device=row['device']) for name,row in base['entries'].items()}
            (out/'original-cpio-inventory.json').write_bytes(wire(inventory));(out/'original-system.dtb').write_bytes(base['fit']['dtb'])
        else:
            from scripts.unified.cross_common import snapshot
            before=snapshot(ROOT);context,dt,fw=rebind_context(base,args.build_id,args.service_default);context['source_before']=before
            if args.mode=='overlay':
                need(args.overlay_plan,'explicit overlay plan required');plan_raw=read_regular(args.overlay_plan,16*1024**2);plan=json.loads(plan_raw)
                need(set(plan)-{'schema','build_id','epoch','files','directories','remove','replace_prefixes'}==set() and {'schema','build_id','epoch','files','directories','remove'}<=set(plan) and type(plan['schema']) is int and plan['schema']==1 and plan['build_id']==args.build_id,'exact overlay plan schema/build ID')
                files={}
                for name,row in plan['files'].items():
                    member_name(name);need(set(row)=={'path','bytes','sha256','mode'},'pinned overlay file fields');files[name]=(read_ref({k:row[k] for k in ('path','bytes','sha256')}),row['mode'])
                needed={'etc/codex-build-id','etc/vxgpud/installation.json','etc/vxgpud/installation.sha256','usr/share/vxgpu/payload-manifest.json','usr/share/vxgpu/catalog.bin','lib/firmware/vxl/catalog-v1.bin','lib/firmware/vxl/execution-v1.bin'}
                need(needed<=set(files),'explicit new build/installation/manifest/catalog/execution files required')
                need(files['etc/codex-build-id'][0]==(args.build_id+'\n').encode(),'new rootfs software build ID')
                remove=sorted(set(plan['remove'])|set(expand_prefix_removals(base['cpio'],plan.get('replace_prefixes',[]),keep=set(files)|set(plan['directories']))))
                newcpio=overlay_newc(base['cpio'],files,plan['directories'],remove,plan['epoch']);entries=cpio_entries(newcpio);newgz=gzip_bytes(newcpio)
                raw=lambda name:entries.get(name,{}).get('data',b'')
                need(raw('lib/firmware/vxl/catalog-v1.bin')==raw('usr/share/vxgpu/catalog.bin')==fw['catalog'] and raw('lib/firmware/vxl/execution-v1.bin')==fw['execution'],'rootfs exact new trusted firmware')
                expected_programs={'lib/firmware/vxl/programs/'+e['entry_id']+'.bin':e['payload'] for e in fw['entries']}
                need({n for n in entries if n.startswith('lib/firmware/vxl/programs/') and entries[n]['kind']==stat.S_IFREG}==set(expected_programs),'exact16 program files; explicit obsolete removal required')
                need(all(raw(name)==value for name,value in expected_programs.items()),'actual rootfs new CODE/RODATA containers')
                payload=json.loads(raw('usr/share/vxgpu/payload-manifest.json'));need(payload['build_id']==args.build_id,'new complete payload manifest build ID')
                admission=payload_errors(normalize_cpio_entries(entries),payload)
                need(not admission,'original payload contract errors: '+repr(admission))
                need(raw('etc/default/vxgpud')==('UNIFIED_SERVICE_ENABLED='+str(args.service_default)+'\n').encode(),'explicit requested startup default bytes')
                context['payload_admission']=audit_rootfs(newcpio,args.build_id)
                need(shutil.disk_usage(out).free>len(newgz)*3+512*1024**2,'container output capacity plus512MiB reserve')
                packed=pack_fit(base['loaded']['image.ub'],dt,newgz,args.build_id,plan['epoch'])
                (out/'rootfs.cpio.gz').write_bytes(newgz);(out/'image.ub').write_bytes(packed)
                tarpath=out/'rootfs.tar.gz';tar_mirror(entries,tarpath,plan['epoch'])
                need(not _cpio.rootfs_payload_errors(newcpio,tarpath),'independent cpio/tar semantic equality')
                for name in ('BOOT.BIN','boot.scr'):(out/name).write_bytes(base['loaded'][name])
                rec['output_files']={n:evidence(read_regular(out/n)) for n in ('rootfs.cpio.gz','rootfs.tar.gz','image.ub','BOOT.BIN','boot.scr')};rec['overlay_plan']=dict(path=str(pathlib.Path(args.overlay_plan).resolve()),**evidence(plan_raw))
                rec['scope']='ACTUAL_CONTAINER_REPACK_ONLY_CONTENT_SOURCE_CROSS_AND_STARTUP_ADMISSION_STILL_REQUIRED'
                rec['pending_gates']=['MATCHING_FROZEN_SOURCE_LOCK_AND_AARCH64_CROSS_RUNTIME_ELF','NEW_MODEL_PACKAGE_HOST_ADMISSION','EXPLICIT_STARTUP_DP_AND_IDLE_SINGLE_OWNER_POLICY','FINAL_RELEASE_AND_TARGET_ACCEPTANCE']
            need(snapshot(ROOT)==before,'R4 source changed during context/container preparation');context['source_after']=before
            write_context(out,context,dt,fw);rec['software_reuse_context']=dict(path=str(out/'software-reuse-context.json'),**evidence((out/'software-reuse-context.json').read_bytes()))
        need(all(digest(read_regular(RELEASE/name))==want for name,want in PINS.items()),'R3 release changed during read-only preparation')
        rec['natural_exit']=0
    except BaseException as error:
        rec.update(error=repr(error),traceback=traceback.format_exc());raise
    finally:
        rec['ended_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();(out/'receipt.json').write_bytes(wire(rec))
    print(json.dumps({k:rec[k] for k in ('scope','natural_exit','ready_for_sd','mode')}))

if __name__=='__main__':main()

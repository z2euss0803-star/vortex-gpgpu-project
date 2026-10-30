"""Inspect actual HW4 BOOT/FIT/DT/rootfs/kernel bytes. No board interaction."""
import argparse
import gzip
import hashlib
import importlib.util
import json
import posixpath
from pathlib import Path
import re
import shutil
import stat
import struct
import subprocess
import sys
import tarfile
from build_common import checked_release, elf_record, native_output, readelf, sha, write_json
from cross_audit import ARTIFACTS, audit_kernel
from package_release import file_bytes

SD_VARIANT='sd-compat-two-flags-trial-a1'
DATA_PREFIX='usr/share/codex-hunet-v5/'
ROOT=Path(__file__).resolve().parents[2]


def legacy_cpio():
    # Reuse the already tested archive parser, without legacy H0 acceptance rules.
    old_dir=ROOT/'scripts/cnn'
    sys.path.insert(0,str(old_dir))
    spec=importlib.util.spec_from_file_location('archive_parser_cnn',old_dir/'image_audit.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module


def bit_payload(raw):
    """Vivado .bit x4k wrapper to unencrypted ZynqMP Bootgen LE words.

    Format reference: Xilinx/bootgen tag2020.1, commitbb38995468d8c830cbbfc5062e903961444c0a3c.
    """
    offset=0
    def counted():
        nonlocal offset
        if offset+2>len(raw):raise ValueError('BIT_HEADER_TRUNCATED')
        n=struct.unpack_from('>H',raw,offset)[0];offset+=2
        value=raw[offset:offset+n];offset+=n
        if len(value)!=n:raise ValueError('BIT_FIELD_TRUNCATED')
        return value
    if counted()!=b'\x0f\xf0'*4+b'\0':raise ValueError('BIT_MAGIC')
    if raw[offset:offset+2]!=b'\0\x01':raise ValueError('BIT_WRAPPER')
    offset+=2
    for tag in b'abcd':
        if offset>=len(raw) or raw[offset]!=tag:raise ValueError('BIT_FIELD')
        offset+=1
        value=counted()
        if not value.endswith(b'\0') or len(value)<2:raise ValueError('BIT_FIELD_STRING')
        if tag==ord('b') and not value.startswith((b'xczu',b'xqzu',b'xazu')):raise ValueError('BIT_DEVICE')
    if raw[offset:offset+1]!=b'e' or offset+5>len(raw):raise ValueError('BIT_LENGTH')
    length=struct.unpack_from('>I',raw,offset+1)[0];offset+=5
    if not length or length%4 or len(raw)!=offset+length:raise ValueError('BIT_PAYLOAD_LENGTH')
    return b''.join(raw[i:i+4][::-1] for i in range(offset,len(raw),4))


def boot_partitions(raw):
    def block(offset,count):
        if offset<0 or offset%4 or offset+4*count>len(raw):raise ValueError('BOOT_HEADER_RANGE')
        return struct.unpack_from('<%dI'%count,raw,offset)
    if len(raw)<0xb8 or block(0x20,2)!=(0xaa995566,0x584c4e58):raise ValueError('BOOT_MAGIC')
    if sum(block(0x20,11))&0xffffffff!=0xffffffff:raise ValueError('BOOT_HEADER_CHECKSUM')
    if block(0x28,1)[0]:raise ValueError('ENCRYPTED_BOOT_UNSUPPORTED')
    iht_at,ph_at=block(0x98,2);iht=block(iht_at,16)
    if sum(iht)&0xffffffff!=0xffffffff:raise ValueError('IHT_CHECKSUM')
    if iht[0]!=0x01020000 or not 1<=iht[1]<=32 or iht[4]:raise ValueError('IHT_VERSION_COUNT_OR_AUTH')
    if iht[2]*4!=ph_at:raise ValueError('BOOT_IHT_PHT_LINK')
    parts=[];ranges=[]
    for index in range(iht[1]):
        offset=ph_at+index*64;words=block(offset,16)
        if sum(words)&0xffffffff!=0xffffffff:raise ValueError('PHT_CHECKSUM')
        start=words[8]*4;size=words[1]*4;total=words[2]*4;attr=words[9]
        if not size or total<size or words[0]!=words[1] or start<ph_at+iht[1]*64 or start+total>len(raw):raise ValueError('BOOT_PARTITION_RANGE')
        if attr&((1<<7)|(1<<15)) or words[13]:raise ValueError('ENCRYPTED_OR_AUTH_PARTITION_UNSUPPORTED')
        if any(start<end and begin<start+total for begin,end in ranges):raise ValueError('BOOT_PARTITION_OVERLAP')
        ranges.append((start,start+total))
        parts.append(dict(index=index,offset=start,bytes=size,total_bytes=total,attributes=attr,
            destination=(attr>>4)&7,payload=raw[start:start+size]))
    return parts


def boot_bit_errors(raw,expected):
    try:
        pl=[p for p in boot_partitions(raw) if p['destination']==2]
        if len(pl)!=1 or pl[0]['payload']!=expected:return ['actual BOOT PL partition differs from frozen HW4 bit']
        return []
    except (ValueError,struct.error) as error:return [str(error)]


def dt_errors(value):
    expected=dict(compatible='vortex,hw4-hunet-v5',resources=[0xa0000000,65536,0xb0010000,65536],
        pool=[0x10000000,0x100000],pool_type='shared-dma-pool',no_map=True,placement=True,
        pool_link=True,ready_link=True,clock_id=71,reset_id=116,gpio_clock_id=71,
        cdma_owners=0,old_gpu_disabled=True,sd_variant=SD_VARIANT,sd_no_1_8_v=True,sd_disable_wp=True)
    errors=['DT '+key for key,want in expected.items() if value.get(key)!=want]
    regions=value.get('memory',[])
    if not regions or any(start<0x580000000 and 0x500000000<start+size for start,size in regions):errors.append('PL GPU window in Linux memory')
    if not any(start<=0x10000000 and 0x40000000<=start+size for start,size in regions):errors.append('PS pool/FIT placement')
    if value.get('bootargs')!='earlycon console=ttyPS0,115200 root=/dev/ram0 rw':errors.append('RAM-rootfs bootargs')
    return errors


def rootfs_binding_errors(files,manifest,assets,public_key):
    errors=[]
    if files.get('etc/codex-build-id')!=(manifest['build_id']+'\n').encode():errors.append('installed build-id')
    try:
        if json.loads(files.get('etc/codex-hunet-manifest.json',b''))!=manifest:errors.append('installed source/profile manifest')
    except (ValueError,TypeError):errors.append('installed manifest malformed')
    for name,expected in assets.items():
        if files.get(DATA_PREFIX+name)!=expected:errors.append('installed asset '+name)
    if not public_key or files.get('home/root/.ssh/authorized_keys')!=public_key:errors.append('installed trusted public key')
    network=files.get('etc/network/interfaces',b'')
    for token in (b'auto eth0',b'iface eth0 inet static',b'address 192.168.13.10',b'netmask 255.255.255.0'):
        if token not in network:errors.append('static eth0 '+token.decode())
    if not files.get('usr/sbin/dropbear') or len(files.get('etc/init.d/dropbear',b''))<10:errors.append('Dropbear access')
    return errors


def resolve_entry(entries,name):
    """Resolve only within the captured rootfs; never consult the host filesystem."""
    seen=set()
    for _ in range(32):
        if name in seen:return None
        seen.add(name);entry=entries.get(name)
        if entry is None:return None
        if entry['kind']!=stat.S_IFLNK:return entry
        try:target=entry['data'].decode('utf-8')
        except UnicodeError:return None
        name=posixpath.normpath(target.lstrip('/') if target.startswith('/') else posixpath.join(posixpath.dirname(name),target))
        if name=='..' or name.startswith('../'):return None
    return None


def resolved_regular_files(entries):
    files={}
    for name in entries:
        entry=resolve_entry(entries,name)
        if entry and entry['kind']==stat.S_IFREG:files[name]=entry['data']
    return files


def ssh_entry_errors(entries):
    errors=[]
    for name,modes,kind in [('home/root',(0o700,0o755),stat.S_IFDIR),('home/root/.ssh',(0o700,),stat.S_IFDIR),('home/root/.ssh/authorized_keys',(0o600,),stat.S_IFREG)]:
        e=entries.get(name)
        if not e or e['kind']!=kind or e['uid']!=0 or e['mode'] not in modes:errors.append('SSH owner/mode '+name)
    binary=resolve_entry(entries,'usr/sbin/dropbear');init=entries.get('etc/init.d/dropbear')
    for e in (binary,init):
        if not e or e['kind']!=stat.S_IFREG or e['uid']!=0 or not e['mode']&0o111 or len(e['data'])<10:errors.append('Dropbear access')
    files=resolved_regular_files(entries)
    match=re.search(rb'^\s*id:([2-5]):initdefault:',files.get('etc/inittab',b''),re.M)
    levels=('S',match.group(1).decode()) if match else ('S',)
    if not any(any(name.startswith('etc/rc'+level+'.d/S') for level in levels) and value['kind']==stat.S_IFLNK and init is not None and resolve_entry(entries,name) is init for name,value in entries.items()):errors.append('Dropbear boot enable')
    return errors


def cdma_driver_owners(nodes):
    owners=0
    for name,value in nodes.items():
        # Channel children do not instantiate the generic Linux CDMA driver.
        if 'xlnx,axi-cdma-1.00.a' not in value.get('compatible','').split():continue
        n=name;available=True
        while True:
            if nodes.get(n,{}).get('status','okay') not in ('okay','ok'):available=False;break
            if n=='/':break
            n=n.rsplit('/',1)[0] or '/'
        if available:owners+=1
    return owners


def dt_properties(dt,tool):
    node='/amba_pl@0/vortex-backend@a0000000';pool='/reserved-memory/vortex-ps@10000000';gpio='/amba_pl@0/gpio@b0020000';sd='/axi/mmc@ff170000'
    def get(n,p,kind='x'):
        return subprocess.check_output([tool,'-t',kind,str(dt),n,p],universal_newlines=True).strip()
    def cells(n,p):return [int(v,16) for v in get(n,p).split()]
    def properties(n):return subprocess.check_output([tool,'-p',str(dt),n],universal_newlines=True).split()
    def children(n):return subprocess.check_output([tool,'-l',str(dt),n],universal_newlines=True).split()
    def spans(values):
        if len(values)%4:raise ValueError('DT_REG_CELLS')
        return [[(values[i]<<32)|values[i+1],(values[i+2]<<32)|values[i+3]] for i in range(0,len(values),4)]
    # Resolve resource phandles and scan the actual tree, not the prepared source.
    pool_ph=cells(pool,'phandle')[0];gpio_ph=cells(gpio,'phandle')[0]
    ready=cells(node,'ready-gpios')
    all_nodes=['/'];position=0;nodes={}
    while position<len(all_nodes):
        n=all_nodes[position];position+=1
        all_nodes += [(n.rstrip('/')+'/'+child) for child in children(n)]
        props=properties(n)
        nodes[n]={p:get(n,p,'s') for p in ('compatible','status') if p in props}
    owners=cdma_driver_owners(nodes)
    if sd not in all_nodes:sd=next(n for n in all_nodes if n.endswith('/mmc@ff170000'))
    return dict(compatible=get(node,'compatible','s'),resources=[v for pair in spans(cells(node,'reg')) for v in pair],
        pool=spans(cells(pool,'reg'))[0],pool_type=get(pool,'compatible','s'),no_map='no-map' in properties(pool),
        placement='vortex,ps-placement-approved' in properties(node),pool_link=cells(node,'memory-region')==[pool_ph],
        ready_link=ready==[gpio_ph,0,0,0],clock_id=cells(node,'clocks')[1],reset_id=cells(node,'resets')[1],
        gpio_clock_id=cells(gpio,'clocks')[1],cdma_owners=owners,
        old_gpu_disabled=get('/amba_pl@0/HPM@a0000000','status','s')=='disabled',
        memory=[region for n in all_nodes if n=='/memory' or n.startswith('/memory@') for region in spans(cells(n,'reg'))],
        sd_variant=get(sd,'codex,sd-compat-variant','s'),sd_no_1_8_v='no-1-8-v' in properties(sd),sd_disable_wp='disable-wp' in properties(sd),
        bootargs=get('/chosen','bootargs','s'))


def kernel_symbol_errors(symbols):
    # poll_gpu is the real static adapter installed in vxl_hw_ops.
    return ['built-in symbol '+symbol for symbol in
        ('vxl_engine_poll','vxl_hunet_attach','poll_gpu','vxl_hunet_command')
        if not re.search(r'^\S+\s+[Tt]\s+'+symbol+r'$',symbols,re.M)]


def audit_image(image_dir,release_dir,out_dir):
    image=Path(image_dir).resolve();release_dir=Path(release_dir).resolve();hybrid=None;asset_root=release_dir
    if (release_dir/'release-hybrid.json').is_file():
        sys.path.insert(0,str(ROOT/'scripts/hunet_hybrid'))
        from hybrid_build import checked_hybrid
        from hybrid_image import hybrid_image_errors
        from cross_audit import HYBRID_SYMBOLS,audit_hybrid_cross,audit_hybrid_kernel
        hybrid=checked_hybrid(release_dir);asset_root=release_dir/'base'
    release=checked_release(asset_root)
    if hybrid:release.update({k:hybrid[k] for k in ('binding_sha256','cpu_binary_sha256')})
    out=native_output(out_dir)
    project=image.parent.parent;base=project.parent;record=dict(scope='HW4_HUNET_IMAGE_OFFLINE_AUDIT',integrity='FAIL',board='NOT_RUN',sd='NOT_TOUCHED',h1='NOT_RUN')
    errors=[]
    if hybrid:record['scope']='HW4_HUNET_HYBRID_IMAGE_OFFLINE_AUDIT'
    try:
        integration=json.loads((base/'integration.json').read_text());manifest=integration['manifest']
        if (base/'build.exit').read_text().strip()!='0' or (base/'package.exit').read_text().strip()!='0':raise ValueError('ACTUAL_BUILD_OR_PACKAGE_EXIT')
        for name,want in json.loads((base/'protected-original.json').read_text()).items():
            if sha(Path(name))!=want:raise ValueError('PROTECTED_PARENT_CHANGED:'+name)
        for key in ('source_commit','source_digest','bit_sha256','xsa_sha256','binding_sha256'):
            if manifest[key]!=release[key]:errors.append('candidate release '+key)
        bit=file_bytes(asset_root,'hardware/top.bit');expected=bit_payload(bit);boot=(image/'BOOT.BIN').read_bytes()
        errors+=boot_bit_errors(boot,expected)
        parts=boot_partitions(boot);record['boot_partitions']=[{**{k:v for k,v in p.items() if k!='payload'},'sha256':hashlib.sha256(p['payload']).hexdigest()} for p in parts]
        # Locate the actual built kernel; reject ambiguous candidates.
        kernel_candidates=list((project/'build/tmp/work/zynqmp_generic-xilinx-linux/linux-xlnx').glob('*-r0/linux-*-build'))
        if len(kernel_candidates)!=1:raise ValueError('ACTUAL_KERNEL_AMBIGUOUS')
        kernel=kernel_candidates[0];native=kernel.parent/'recipe-sysroot-native/usr/bin'
        fdtget=str(native/'fdtget');dumpimage=str(native/'dumpimage')
        props=dt_properties(image/'system.dtb',fdtget);record['dt']=props;errors+=dt_errors(props)
        for index,name in enumerate(('fit-kernel.bin','fit-system.dtb','fit-rootfs.bin')):
            subprocess.check_call([dumpimage,'-T','flat_dt','-p',str(index),'-o',str(out/name),str(image/'image.ub')],stdout=subprocess.DEVNULL)
        if gzip.decompress((out/'fit-kernel.bin').read_bytes())!=(image/'Image').read_bytes():errors.append('actual FIT kernel bytes')
        if (out/'fit-system.dtb').read_bytes()!=(image/'system.dtb').read_bytes():errors.append('actual FIT DT bytes')
        if (out/'fit-rootfs.bin').read_bytes()!=(image/'rootfs.cpio.gz').read_bytes():errors.append('actual FIT rootfs bytes')
        # Verify FIT-declared SHA256 for all extracted images.
        for node_name,name in [('kernel@1','fit-kernel.bin'),('fdt@system-top.dtb','fit-system.dtb'),('ramdisk@1','fit-rootfs.bin')]:
            fit_node='/images/'+node_name
            children=subprocess.check_output([fdtget,'-l',str(image/'image.ub'),fit_node],universal_newlines=True).split()
            checks=0
            for child in children:
                n=fit_node+'/'+child
                if subprocess.check_output([fdtget,'-t','s',str(image/'image.ub'),n,'algo'],universal_newlines=True).strip()=='sha256':
                    value=subprocess.check_output([fdtget,'-t','bx',str(image/'image.ub'),n,'value'],universal_newlines=True).split()
                    if bytes(int(v,16) for v in value).hex()!=sha(out/name):errors.append('FIT hash '+node_name)
                    checks+=1
            if checks!=1:errors.append('FIT SHA256 coverage '+node_name)
        old=legacy_cpio();cpio=gzip.decompress((out/'fit-rootfs.bin').read_bytes());entries=old.cpio_entries(cpio)
        recipe=project/'components/yocto/layers/core/meta/classes/image_types.bbclass';recipe_sha=sha(recipe)
        errors+=old.rootfs_payload_errors(cpio,image/'rootfs.tar.gz',recipe_sha)
        if recipe_sha!=old.CPIO_INIT_RECIPE_SHA256:errors.append('unrecognized rootfs init recipe')
        files=resolved_regular_files(entries)
        if hybrid:
            assets={name:file_bytes(release_dir,name) for name in hybrid['hybrid_files']};assets['release-hybrid.json']=(release_dir/'release-hybrid.json').read_bytes()
        else:
            assets={name:file_bytes(release_dir,name) for name in release['files']};assets['release.json']=(release_dir/'release.json').read_bytes()
        errors+=rootfs_binding_errors(files,manifest,assets,Path(integration['trusted_public_key']).read_bytes())
        errors+=ssh_entry_errors(entries)
        cfg=(kernel/'.config').read_text();symbols=(kernel/'System.map').read_text()
        for option in ('CONFIG_VXL_LINUX=y','CONFIG_ZONE_DMA32=y','CONFIG_CRYPTO_SHA256=y'):
            if option not in cfg.splitlines():errors.append('kernel '+option)
        errors+=kernel_symbol_errors(symbols)
        kernel_bytes=(image/'Image').read_bytes()
        if release['source_commit'].encode() not in kernel_bytes or bytes.fromhex(release['source_digest']) not in kernel_bytes:errors.append('actual Image compiled source binding')
        actual_kernel=json.loads((base/'actual-kernel/receipt.json').read_text());errors+=(audit_hybrid_kernel(actual_kernel,hybrid) if hybrid else audit_kernel(actual_kernel,release))['errors']
        if actual_kernel.get('scope')!='ACTUAL_IMAGE_KERNEL_CROSS' or actual_kernel['kernel_build']!=str(kernel) or actual_kernel['config_sha256']!=sha(kernel/'.config') or actual_kernel['symvers_sha256']!=sha(kernel/'Module.symvers'):errors.append('actual kernel receipt binding')
        actual=project/'build/tmp/work-shared/zynqmp-generic/kernel-source/drivers/misc/vxl'
        inventory=json.loads(file_bytes(asset_root,'SOURCE_SHA256.json'))['files']
        for name,want in inventory.items():
            if name.split('/')[0] not in ('driver','core','include','generated'):continue
            path=actual/name
            if name=='driver/Makefile':
                expected=integration['kernel_makefile'].encode()
                if not path.is_file() or path.read_bytes()!=expected:errors.append('actual kernel Makefile')
            elif not path.is_file() or sha(path)!=want['sha256']:errors.append('actual compiled source '+name)
        if (actual/'driver/vxl_hunet_source.h').read_bytes()!=file_bytes(asset_root,'build/vxl_hunet_source.h'):errors.append('actual kernel source header')
        if hybrid and (actual/'driver/vxl_hybrid_release.h').read_bytes()!=file_bytes(release_dir,'build/vxl_hybrid_release.h'):errors.append('actual hybrid source header')
        (out/'tools').mkdir();record['elfs']={}
        for name in ARTIFACTS+(('libvortex-hybrid.so','vxl_hunet_hybrid') if hybrid else ()):
            installed='usr/lib/'+name if name.endswith('.so') else 'usr/bin/'+name
            cpu=hybrid and name=='vxl_hunet_hybrid'
            target=out/'tools'/name;target.write_bytes(files[installed]);value=elf_record('readelf',target,release,bound=not cpu);record['elfs'][name]=value
            if value['machine']!='AArch64' or (not cpu and (value['source_commit']!=release['source_commit'] or value['source_digest']!=release['source_digest'])):errors.append('installed ELF '+name)
            if cpu and value['sha256']!=hybrid['cpu_binary_sha256']:errors.append('installed frozen CPU ELF')
            if not name.endswith('.so') and value['interpreter']!='/lib/ld-linux-aarch64.so.1':errors.append('installed ELF loader '+name)
        # Reuse the dependency/ABI-version closure against the actual target rootfs.
        from cross_check import runtime_inventory
        (out/'runtime').mkdir();needed={n for v in record['elfs'].values() for n in v['needed']}
        libs,versions=runtime_inventory(image/'rootfs.tar.gz',needed,out/'runtime','readelf')
        for name,value in record['elfs'].items():
            if not set(value['required_versions']).issubset(versions):errors.append('installed runtime symbol versions '+name)
        if hybrid:
            cross_path=Path(integration['cross_receipt']);cross=json.loads(cross_path.read_bytes())
            if sha(cross_path)!=integration['cross_receipt_sha256'] or audit_hybrid_cross(cross,hybrid)['integrity']!='PASS':errors.append('image actual cross receipt')
            canonical=file_bytes(release_dir,'binding-hybrid.bin')
            record.update(hybrid_binding_sha256=hashlib.sha256(canonical).hexdigest() if canonical in kernel_bytes and canonical in (image/'vmlinux').read_bytes() else None,cpu_elf_sha256=record['elfs']['vxl_hunet_hybrid']['sha256'],program8_code_sha256=hashlib.sha256(files[DATA_PREFIX+'program8/code.bin']).hexdigest(),hybrid_builtin_symbols=[name for name in HYBRID_SYMBOLS if re.search(r'^\S+\s+[Tt]\s+'+name+r'$',symbols,re.M)],hybrid_library_source_bound=record['elfs']['libvortex-hybrid.so']['source_digest']==hybrid['source_digest'] and record['elfs']['libvortex-hybrid.so']['source_commit']==hybrid['source_commit'],runtime_libm_sha256=libs.get('libm.so.6',{}).get('sha256'),cross_libm_sha256=cross['runtime_files'].get('libm.so.6',{}).get('sha256'))
            errors+=hybrid_image_errors(record,hybrid)
            if sha(image/'BOOT.BIN')!=integration['retained_boot_sha256']:errors.append('retained HW4 BOOT.BIN')
        script=(image/'boot.scr').read_bytes();errors+=boot_script_errors(script)
        if 0x80000+len(kernel_bytes)>=0x10000000 or 0x20000000+(image/'image.ub').stat().st_size>=0x40000000:errors.append('boot load/pool collision')
        record.update(source_commit=release['source_commit'],source_digest=release['source_digest'],build_id=manifest['build_id'],bit_sha256=release['bit_sha256'],xsa_sha256=release['xsa_sha256'],
            files={name:dict(bytes=(image/name).stat().st_size,sha256=sha(image/name)) for name in ('BOOT.BIN','boot.scr','image.ub','system.dtb','Image','rootfs.tar.gz','rootfs.cpio.gz')},runtime_files=libs)
    except Exception as error:errors.append(str(error))
    record['errors']=errors;record['integrity']='FAIL' if errors else 'PASS';write_json(out/'receipt.json',record)
    if errors:raise ValueError(errors)
    print('HW4_HUNET_IMAGE_OFFLINE_AUDIT_PASS_NOT_BOARD',out,flush=True)
    return record


def boot_script_errors(raw):
    import zlib
    if len(raw)<73:return ['boot.scr truncated']
    header=bytearray(raw[:64]);checksum=int.from_bytes(header[4:8],'big');header[4:8]=bytes(4)
    if int.from_bytes(header[:4],'big')!=0x27051956 or zlib.crc32(header)!=checksum or zlib.crc32(raw[64:])!=int.from_bytes(header[24:28],'big'):return ['boot.scr CRC']
    try:text=raw[72:].decode('ascii')
    except UnicodeError:return ['boot.scr encoding']
    required=('root=/dev/ram0 rw','fatload mmc 1:1 0x20000000 image.ub','setenv fdt_high 0x0fffffff','setenv initrd_high 0x3fffffff','bootm 0x20000000')
    if '\r' in text or not text.endswith('exit 1\n') or any(s not in text for s in required):return ['boot.scr SD/RAM contract']
    return []


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--image',type=Path,required=True);p.add_argument('--release',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    audit_image(a.image,a.release,a.out)

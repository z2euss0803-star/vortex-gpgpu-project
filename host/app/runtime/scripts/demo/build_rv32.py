"""Program12 RV32 native signed INT16 PE + independent IN-v3 cross build.

The linked lookup table has its own read-only PT_LOAD and sidecar upload.
This never executes RV32, operates hardware, or writes removable media.
"""
import argparse,hashlib,json,pathlib,shutil,struct,subprocess,sys
from scripts.demo.mosaic_rv32_contract import plan_runtime
from scripts.mosaic.contract import build_graph
from scripts.mosaic.layout import plan_layout
from scripts.hunet_opt.build_rv32 import DEFINES,clang_resource_dir,static_frame
from scripts.hunet.audit_program import elf_info,instructions
from scripts.s8.audit_rv32 import sections,call_graph,array_callbacks
CPP_UNITS=('main','entry_context','graph','conv','point_ops_compat_v3','quant_pack','tile','unpack','isa_adapter','instance_norm_compat_v2','instance_norm_compat_v3')
C_UNITS=('core/mosaic_layout.c','core/mosaic_validate.c')
def sha(path):return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()
def catalog_header(model,assets):
    raw=pathlib.Path(assets).read_bytes()
    if raw[:8]!=b'MS8AST1\0' or len(raw)!=struct.unpack_from('<Q',raw,24)[0]:raise ValueError('native asset identity/layout')
    rows=[];catalog=[]
    for w,h in ((256,256),(1280,960)):
        graph=build_graph(model,w,h);layout=plan_layout(graph);catalog.append(dict(graph=graph,layout=layout))
        tag=lambda s:'{'+','.join(str(v) for v in bytes.fromhex(s)[:16])+'}'
        region=lambda r:'{'+','.join(str(v) for v in (r['address'],r['bytes'],r['guard'],int(r['readonly'])))+'}'
        line='{'+str(w)+','+str(h)+','+tag(layout['sha256'])+','+tag(graph['sha256'])+',{'+str(w)+','+str(h)+',{'+','.join(region(r) for r in layout['regions'])+'},'+str(layout['end'])+'},{'
        params=[]
        for i,layer in enumerate(graph['layers']):
            record=256+i*384;values=[]
            for j in range(5 if i!=15 else 3):
                offset,size=struct.unpack_from('<QQ',raw,record+64+j*64+8);values.append((offset,size))
            if values!=[(s['offset'],s['bytes']) for s in layer['weight_segments']]:raise ValueError('actual weights differ from graph offsets')
            address=lambda i,t:'reinterpret_cast<const '+t+' *>(uintptr_t('+str(0x80800000+values[i][0])+'))'
            scale=struct.unpack_from('<f',raw,values[2][0])[0]
            params.append('{'+address(0,'int16_t')+','+str(values[0][1]//2)+','+address(1,'float')+','+str(values[1][1]//4)+','+scale.hex()+'f,'+(address(3,'float')+','+address(4,'float') if i!=15 else 'nullptr,nullptr')+'}')
        rows.append(line+','.join(params)+'}}')
    return '#include "entry.h"\nstatic const mosaic_entry_catalog mosaic_device_catalog[]={\n'+',\n'.join(rows)+'\n};\nstatic const size_t mosaic_device_catalog_count=2;\n',catalog

def build(env_path,root,out):
    env=json.loads(pathlib.Path(env_path).read_text());root=pathlib.Path(root).resolve();out=pathlib.Path(out).resolve();out.mkdir(parents=True,exist_ok=False)
    result=dict(scope='ACTUAL_MOSAIC_RV32_LINK_NOT_EXECUTED',natural_exit=1,commands=[],sources={},dependencies={},gpu_raw_cycles=None)
    def run(argv,label):
        argv=[str(x) for x in argv];q=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(out/(label+'.log')).write_bytes(q.stdout);result['commands'].append(dict(argv=argv,natural_exit=q.returncode,log=label+'.log'));(out/'receipt.json').write_text(json.dumps(result,indent=2)+'\n')
        if q.returncode:raise ValueError(label+' failed: '+q.stdout.decode('utf-8','replace')[-1800:])
    try:
        tools=pathlib.Path(env['rv32_tools']);gnu=tools/'riscv32-gnu-toolchain';clang=tools/'llvm-vortex/bin/clang++';gcc=gnu/'bin/riscv32-unknown-elf-gcc';ar=gnu/'bin/riscv32-unknown-elf-gcc-ar';sdk=root/'vendor/hunet/sdk-linux-crt'
        version=subprocess.check_output([str(clang),'--version']).decode();assert '18.1.7' in version;result['compiler_version']=version
        gen=out/'generated';gen.mkdir();assets=pathlib.Path(env['mosaic_assets']);model=json.loads((root/'config/mosaic/model-v1.json').read_text());header,catalog=catalog_header(model,assets);(gen/'mosaic_device_catalog.h').write_text(header);(out/'catalog-layouts.json').write_text(json.dumps(catalog,indent=2)+'\n')
        norm=json.loads((root/'config/mosaic/norm-compat-v3.json').read_text());table_paths=[]
        for key in ('rcp.f32','rsqrt.bin'):
            row=norm['tables'][key];path=pathlib.Path(row['remote_path']);assert path.stat().st_size==row['bytes'] and sha(path)==row['sha256'];table_paths.append(path);result['dependencies'][str(path)]=row['sha256']
        table=(gen/'norm-tables.S');table.write_text('.section .mosaic_norm_lookup,"a",@progbits\n.balign 64\n.global mosaic_norm_rcp_count_v2\n.type mosaic_norm_rcp_count_v2,@object\nmosaic_norm_rcp_count_v2:\n.incbin "'+str(table_paths[0])+'"\n.size mosaic_norm_rcp_count_v2, .-mosaic_norm_rcp_count_v2\n.global mosaic_norm_rsqrt_delta_v2\n.type mosaic_norm_rsqrt_delta_v2,@object\nmosaic_norm_rsqrt_delta_v2:\n.incbin "'+str(table_paths[1])+'"\n.size mosaic_norm_rsqrt_delta_v2, .-mosaic_norm_rsqrt_delta_v2\n')
        defines=[x for x in DEFINES if not x.startswith(('-DIO_MPM_ADDR=','-DHUNET_SCALAR_CHUNK='))]+['-DIO_MPM_ADDR=0x10002000']
        includes=['-I'+str(x) for x in (gen,root/'include',root/'device/mosaic_s8',sdk/'kernel/include',sdk/'kernel/src',sdk/'sim/common',sdk/'hw')]
        flags=['-march=rv32imaf','-mabi=ilp32f','-O2','-mcmodel=medany','-fno-fast-math','-ffp-contract=off','-fstack-usage','-fdata-sections','-ffunction-sections']+defines+includes
        cxx=['--sysroot='+str(gnu/'riscv32-unknown-elf'),'--gcc-toolchain='+str(gnu),'-resource-dir='+str(clang_resource_dir(tools)),'-Xclang','-target-feature','-Xclang','+vortex','-Xclang','-target-feature','-Xclang','+zicond','-mllvm','-disable-loop-idiom-all','-std=c++17','-fno-exceptions','-fno-rtti']+flags
        archive=out/'libvortex.a';shutil.copyfile(env['vortex_archive'],archive);members=subprocess.check_output([str(ar),'t',str(archive)]).decode().splitlines();old=[x for x in members if x.split('.')[0] in ('vx_start','vx_print','vx_perf','vx_spawn','vx_syscalls')]
        if old:run([ar,'d',archive]+old,'archive-prune')
        units=[(root/'device/mosaic_s8/crt/vx_start.S',gcc,flags),(sdk/'kernel/src/vx_spawn.c',gcc,flags),(sdk/'kernel/src/vx_syscalls.c',gcc,flags),(root/'device/s8/crt/perf_stub.c',gcc,flags)]+[(root/n,gcc,flags) for n in C_UNITS]+[(root/('device/mosaic_s8/'+n+'.cpp'),clang,cxx) for n in CPP_UNITS]
        objects=[];owners={}
        for i,(source,compiler,f) in enumerate(units):
            obj=out/('%02d-'%i+source.name+'.o');options=list(f)
            if source.name in ('instance_norm_compat_v2.cpp','instance_norm_compat_v3.cpp'):options+=['-DMOSAIC_NORM_COMPAT_EXPLICIT_ISA_FMA','-include',str(root/'device/mosaic_s8/norm_explicit_fma_prefix.h')]
            run([compiler]+options+['-MMD','-MF',str(obj)+'.d','-c',source,'-o',obj],'compile-%02d'%i);objects.append(obj);result['sources'][str(source.relative_to(root))]=sha(source)
            ob=obj.read_bytes();oh=struct.unpack_from('<HHIIIIIHHHHHH',ob,16);_,functions,_=sections(ob,oh)
            for n in functions:owners.setdefault(n,[]).append(source.name)
        data=out/'norm-data.o';run([clang,'-cc1as','-triple','riscv32-unknown-elf','-target-feature','+m','-target-feature','+a','-target-feature','+f','-target-abi','ilp32f','-filetype','obj','-o',data,table],'table-assemble');objects.append(data)
        script=root/'device/mosaic_s8/crt/link32.ld';elf=out/'kernel.elf';libm=gnu/'riscv32-unknown-elf/lib/libm.a'
        run([clang]+cxx+['-nostartfiles','-nostdlib']+objects+['-Wl,-Bstatic,--gc-sections,--emit-relocs,-T,'+str(script)+',--defsym=STARTUP_ADDR=0x80000000,-Map,'+str(out/'kernel.map'),archive,libm,'-L'+str(tools/'libc32/lib'),'-lc',tools/'libcrt32/lib/baremetal/libclang_rt.builtins-riscv32.a','-o',elf],'link')
        raw,eh,loads,symbols=elf_info(elf);assert eh[6]&6==2
        end=symbols['_end']['value'];tls=symbols['__tbss_size']['value'];assert symbols['__tdata_size']['value']==0
        low=[x for x in loads if x['start']<0x80100000];table_load=[x for x in loads if x['start']>=0x80110000]
        assert low and all(x['end']<=0x800fffc0 for x in low) and len(table_load)==1 and table_load[0]['start']==0x80110000 and table_load[0]['end']==0x80110000+6307840 and not table_load[0]['flags']&2
        code_bytes=(end-0x80000000+63)//64*64;flat=bytearray(code_bytes)
        for segment in low:
            off=segment['start']-0x80000000;flat[off:off+segment['file_bytes']]=raw[segment['offset']:segment['offset']+segment['file_bytes']]
        ro=table_load[0];robytes=raw[ro['offset']:ro['offset']+ro['file_bytes']];assert robytes==b''.join(x.read_bytes() for x in table_paths)
        (out/'code.bin').write_bytes(flat);(out/'norm-rodata.bin').write_bytes(robytes)
        disasm=subprocess.check_output([str(tools/'llvm-vortex/bin/llvm-objdump'),'-d',str(elf)]);(out/'kernel.disasm').write_bytes(disasm)
        named,functions,undefined=sections(raw,eh);assert not undefined,undefined
        frames={}
        for path in out.glob('*.su'):
            for line in path.read_text().splitlines():
                name,size,kind=line.rsplit('\t',2);assert kind=='static','dynamic stack '+name;frames[name.split(':')[-1]]=int(size)
        native=[n for n in functions if any(t in n for t in ('native_begin','native_step','native_read','native_release','native_fence','native_now','native_broadcast'))]
        callbacks=[n for n in functions if 'callbackPv' in n];assert native and callbacks
        indirect={}
        for n in functions:
            owner=owners.get(n.split('@')[0],[])
            if set(owner)&{'tile.cpp','conv.cpp','point_ops_compat_v3.cpp'}:indirect[n]=native
            if 'vx_spawn.c' in owner:indirect[n]=callbacks
        stack,wordmap=call_graph(raw,loads,functions,owners,frames,array_callbacks(raw,named,functions),indirect)
        (out/'ownership.json').write_text(json.dumps(owners,indent=2)+'\n');(out/'stack-audit.json').write_text(json.dumps(stack,indent=2)+'\n')
        words=[];conservative_frame_sum=0
        for name,s in symbols.items():
            if not s['size'] or not any(x['flags']&1 and x['start']<=s['value']<x['end'] for x in low):continue
            ins=instructions(raw,loads,symbols,name,s['size']//4);words.extend(ins)
            if name not in ('_start','init_regs','init_regs_all','init_tls_all'):conservative_frame_sum+=static_frame(ins)
        assert 0x00301073 in instructions(raw,loads,symbols,'init_regs',32),'FCSR not initialized on all hart paths'
        assert any(w&0x7f==0x43 for w in words),'explicit native IN FMA absent'
        assert any(w&0xfe00707f==0x0400000b for w in words),'native S8 begin absent'
        runtime=plan_runtime(end-0x80000000,tls,stack['bytes'])
        result.update(natural_exit=0,runtime=runtime,stack=stack,conservative_frame_sum=conservative_frame_sum,loads=loads,flat=dict(bytes=len(flat),sha256=hashlib.sha256(flat).hexdigest()),rodata=dict(bytes=len(robytes),sha256=hashlib.sha256(robytes).hexdigest(),gpu=0x80110000),elf=dict(bytes=len(raw),sha256=sha(elf)),native_fma_instructions=sum(w&0x7f==0x43 for w in words))
        result['dependencies'].update({str(x):sha(x) for x in (clang,gcc,pathlib.Path(env['vortex_archive']),libm,script,assets)})
    except Exception as exc:result.update(error=str(exc),natural_exit=1)
    (out/'receipt.json').write_text(json.dumps(result,indent=2)+'\n')
    if result['natural_exit']:raise ValueError(result['error'])
    return result
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--env',required=True);parser.add_argument('--root',default='.');parser.add_argument('--out',required=True);a=parser.parse_args();r=build(a.env,a.root,a.out);print(json.dumps(dict(scope=r['scope'],natural_exit=r['natural_exit'],runtime=r['runtime'],gpu_raw_cycles=None)))

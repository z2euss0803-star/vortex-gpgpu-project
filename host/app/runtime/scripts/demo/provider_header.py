"""Trusted provider file identities, with explicit self-hash cycle exclusions."""
import struct
from scripts.demo.mosaic_catalog import digest,c_array
from scripts.demo.dual_binding import _checked_hunet
from scripts.mosaic.binding import ORDER,make_binding
REQUIRED_FILES=frozenset(('mosaic/program12/code.bin','mosaic/program12/norm-rodata.bin','mosaic/weights.ms8','mosaic/input.f32','display/hunet-labels.bin','display/hunet-glyphs-u8.bin','display/hunet-source-roi0.rgb','display/hunet-historical-logits.f32','display/hunet-historical-probe.json','hunet/input-roi0.f32','hunet/program9/code.bin','hunet/base/weights.bin','hunet/base/model.onnx','hunet/base/matrix_descriptors.bin','hunet/cpu-contract.json','hunet/cpu-stages.json','hunet/jobs.bin','hunet/fusion-contract.json','hunet/descriptors.bin','hunet/layout.json'))
def provider_header(files,mosaic,hunet,classes,font,libm):
    if set(files)!=REQUIRED_FILES:raise ValueError('exact provider files; no self-dependent binding/CPU/numeric-gate hashes')
    if len(mosaic)!=512 or mosaic[320:352]!=bytes(32):raise ValueError('runner SHA must be deferred')
    values={n:mosaic[32+i*32:64+i*32].hex() for i,n in enumerate(ORDER)};values['runner']='01'*32
    check=bytearray(make_binding(values));check[320:352]=bytes(32)
    if bytes(check)!=mosaic:raise ValueError('Mosaic template header/reserved/identity')
    if len(hunet)!=512 or hunet[256:288]!=bytes(32):raise ValueError('CPU SHA must be deferred')
    check=bytearray(hunet);check[256:288]=bytes([1])*32;_checked_hunet(bytes(check))
    text='#ifndef DEMO_RELEASE_ASSETS_H\n#define DEMO_RELEASE_ASSETS_H\n#include <stdint.h>\nstruct demo_release_file {const char *path;uint32_t bytes;uint8_t sha[32];};\nstatic const struct demo_release_file demo_release_files[]={\n'
    for name,r in sorted(files.items()):
        if set(r)!={'bytes','sha256'} or type(r['bytes'])!=int or not 0<r['bytes']<=67108864:raise ValueError('file extent/identity')
        text+=' {"'+name+'",'+str(r['bytes'])+','+c_array(digest(r['sha256']))+'},\n'
    text+='};\n#define DEMO_RELEASE_FILE_COUNT '+str(len(files))+'u\n'
    for name,value in (('mosaic_binding_template',mosaic),('hunet_binding_template',hunet)):text+='static const uint8_t demo_release_'+name+'[512]='+c_array(value)+';\n'
    for name,value in (('classes',classes),('font',font)):
        digest(value);text+='static const char demo_release_'+name+'_sha[]="'+value+'";\n'
    return text+'static const uint8_t demo_release_libm_sha[32]='+c_array(digest(libm))+';\n#endif\n'

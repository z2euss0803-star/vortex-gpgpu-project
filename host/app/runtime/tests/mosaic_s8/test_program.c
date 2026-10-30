#include <vxl/mosaic_host.h>
#include <vxl/hunet.h>
#include "mosaic_fixture.h"
#include "mosaic_contract_fixture.h"
#include "program_fixture.h"
#include <assert.h>
#include <stdio.h>
_Static_assert(sizeof(struct vxl_request)==176,"old public request unchanged");
int main(void){
 struct vxl_engine e={0};struct mosaic_ctx c;struct mosaic_host_state h;
 struct mosaic_mutable mutable[2]={{128,64},{256,1792}};
 struct mosaic_catalog_layout catalog={0};catalog.layout=fixture_small_layout;
 memcpy(catalog.graph_sha,program_graph,32);memcpy(catalog.layout_sha,program_layout,32);
 struct mosaic_program p={.flat=program_flat,.flat_bytes=256,.runtime_bytes=2048,.entry=0x80000000,.mutable_ranges=mutable,.mutable_count=2,.layouts=&catalog,.layout_count=1,.assets=&fixture_manifest,.assets_bytes=3382784,.crt_base=0x10002000,.crt_bytes=4096,.tls_base=0x80000200,.tbss_bytes=4,.tbss_stride=16,.tbss_align=16,.stack_stride=8192,.static_stack_bytes=1024,.stack_base=0xfff00000,.stack_bytes=0x100000};
 memcpy(&p.binding,program_binding,512);memcpy(p.binding_sha,program_binding_sha,32);memcpy(p.graph_policy_sha,program_policy,32);memcpy(p.runtime_deps_sha,program_deps,32);
 e.profile_id=6;assert(!mosaic_host_init(&c,&h,&e,program_binding_sha));
 vxl_u8 digest[32];mosaic_program_catalog_hash(&p,0,digest);assert(!memcmp(digest,p.binding.digests[MOSAIC_LAYOUT_SHA],32));
 mosaic_program_catalog_hash(&p,1,digest);assert(!memcmp(digest,p.binding.digests[MOSAIC_GRAPH_SHA],32));
 mosaic_program_runtime_hash(&p,digest);assert(!memcmp(digest,p.binding.digests[MOSAIC_RUNTIME_SHA],32));
 assert(!mosaic_program_register(&c,&p));assert(h.program==&p);
 for(unsigned i=0;i<14;++i){struct mosaic_program bad=p;bad.binding.digests[i][0]^=1;assert(mosaic_program_register(&c,&bad)<0);assert(h.program==&p);}
 struct mosaic_program bad=p;bad.entry+=4;assert(mosaic_program_register(&c,&bad)<0);
 bad=p;bad.binding.profile=5;assert(mosaic_program_register(&c,&bad)<0);
 bad=p;bad.binding.program=11;assert(mosaic_program_register(&c,&bad)<0);
 bad=p;bad.binding.reserved[31]=1;assert(mosaic_program_register(&c,&bad)<0);
 vxl_u8 longer[257]={0};memcpy(longer,program_flat,256);bad=p;bad.flat=longer;bad.flat_bytes=257;assert(mosaic_program_register(&c,&bad)<0);
 vxl_u8 changed[256];memcpy(changed,program_flat,256);changed[0]^=1;bad=p;bad.flat=changed;assert(mosaic_program_register(&c,&bad)<0);
 struct mosaic_manifest wrong_model=fixture_manifest;wrong_model.source_sha[0][0]^=1;bad=p;bad.assets=&wrong_model;assert(mosaic_program_register(&c,&bad)<0);
 wrong_model=fixture_manifest;wrong_model.package_sha[0]^=1;assert(mosaic_program_register(&c,&bad)<0);
 bad=p;bad.tbss_stride=0;assert(mosaic_program_register(&c,&bad)<0);
 bad=p;bad.runtime_bytes=0x100000;assert(mosaic_program_register(&c,&bad)<0);
 bad=p;bad.assets_bytes=1;assert(mosaic_program_register(&c,&bad)<0);
 catalog.layout.region[MOSAIC_SKIP].bytes-=64;assert(mosaic_program_register(&c,&p)<0);catalog.layout=fixture_small_layout;
 mutable[0].offset=0;assert(mosaic_program_register(&c,&p)<0);mutable[0].offset=128;
 h.nonce_locked=1;assert(mosaic_program_register(&c,&p)==-EBUSY);h.nonce_locked=0;
 h.loaded=1;assert(mosaic_program_register(&c,&p)==-EBUSY);h.loaded=0;
 h.frame_active=1;assert(mosaic_program_register(&c,&p)==-EBUSY);h.frame_active=0;
 h.expected_binding_sha[0]^=1;assert(mosaic_program_register(&c,&p)<0);h.expected_binding_sha[0]^=1;
 e.profile_id=5;assert(mosaic_program_register(&c,&p)<0);e.profile_id=6;
 assert(!mosaic_program_register(&c,&p));
 e.profile_id=4;assert(mosaic_host_init(&c,&h,&e,program_binding_sha)<0);
 assert(c.host==&h&&h.program==&p);
 e.profile_id=6;e.opened=1;assert(mosaic_host_init(&c,&h,&e,program_binding_sha)==-EBUSY);assert(h.program==&p);
 puts("TEST_ONLY_MOSAIC_PROGRAM_REGISTRY_METADATA_PASS_NOT_ENGINE_NOT_GPU");return 0;
}

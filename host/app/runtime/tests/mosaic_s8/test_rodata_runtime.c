#define MOSAIC_SESSION_SHIM
#include "test_client.c"
static unsigned char *ro;
static void bind_sidecar(const char *assets){
 setup(assets);ro=calloc(1,6307840);assert(ro);for(unsigned i=0;i<6307840;i++)ro[i]=(unsigned char)(i*17);
 program.rodata=ro;program.rodata_bytes=6307840;program.rodata_gpu=0x80110000;vxl_hunet_hash(ro,6307840,program.rodata_sha);
 mosaic_program_runtime_hash(&program,program.binding.digests[MOSAIC_RUNTIME_SHA]);vxl_hunet_hash(&program.binding,512,program.binding_sha);
 memcpy(f.code+0x110000,ro,6307840);
}
int main(int argc,char **argv){
 assert(argc==2);
 /* Runtime must reject an initially corrupted sidecar before START. */
 bind_sidecar(argv[1]);f.code[0x110000]^=1;struct vxl_mosaic_seal_v1 s=seal();memcpy(s.binding_sha,program.binding_sha,32);
 assert(mosaic_runtime_seal(&c,&s)<0&&!f.starts&&e.quarantined);free(ro);cleanup();
 /* Loader must copy the table, while every byte of the parameter gap is denied. */
 bind_sidecar(argv[1]);e.ops=&ops;memset(e.objects,0,sizeof(e.objects));e.pools[0].capacity=VXL_PS_SIZE;e.pools[1].capacity=VXL_PL_SIZE;
 memset(f.code+0x110000,0,6307840);struct mosaic_client client;assert(!mosaic_client_attach_fd(&client,55));
 assert(!mosaic_client_configure(&client,&program,f.weights,3382784,0));unsigned before=code_loads;
 assert(!mosaic_client_load(&client,&fixture_manifest));assert(code_loads==before+2);assert(!memcmp(f.code+0x110000,ro,6307840));
 struct vxl_request req=request();req.handle=e.objects[0].handle;req.offset=0x110000;req.length=16;req.user_ptr=(uintptr_t)ro;assert(call(VXL_COPY_TO,&req)==-EACCES);
 /* A table fault after sealing is caught by immutable checking before START. */
 struct vxl_mosaic_frame_v1 fr=frame();memcpy(fr.binding_tag,program.binding_sha,16);assert(!mosaic_runtime_begin(&c,&fr));
 struct mosaic_job_v1 j;assert(!mosaic_next_job(&c,1,&j));memcpy(h.args_shadow,&j,128);h.submit_enter_ns=now(NULL);f.code[0x110000+63]^=1;
 struct vxl_prepared prepared;assert(mosaic_prepare(&e,&prepared)<0&&e.quarantined&&e.job.pinned&&!f.starts);
 free(ro);cleanup();puts("MOSAIC_SIDECAR_REAL_CLIENT_LOAD_AND_RUNTIME_IMMUTABLE_FAULT_PASS_NOT_GPU raw_gpu_cycles=null");
}

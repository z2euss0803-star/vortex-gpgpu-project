#include <vxl/v2/program.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
struct program_mem {unsigned live;};
static int program_alloc(void *p,struct vxl_v2_object *o){o->storage=malloc((size_t)o->allocated);if(!o->storage)return -ENOMEM;memset(o->storage,0xa5,(size_t)o->allocated);((struct program_mem *)p)->live++;return 0;}
static int program_free(void *p,struct vxl_v2_object *o){free(o->storage);((struct program_mem *)p)->live--;return 0;}
static int program_copy(void *p,struct vxl_v2_object *o,vxl_u64 off,void *b,vxl_u64 n,int w){(void)p;assert(off+n<=o->allocated);if(w)memcpy((unsigned char *)o->storage+off,b,(size_t)n);else memcpy(b,(unsigned char *)o->storage+off,(size_t)n);return 0;}
static const struct vxl_v2_memory_ops program_ops={program_alloc,program_free,program_copy};
static size_t read_fixture(const char *path,unsigned char *raw,size_t max){FILE *f=fopen(path,"rb");size_t n;assert(f);n=fread(raw,1,max,f);assert(feof(f));fclose(f);return n;}
static struct vxl_v2_provider test_provider(void){struct vxl_v2_provider p={.flags=1};memset(p.hardware_contract,1,32);memset(p.bit,2,32);memset(p.xsa,3,32);return p;}

#include <vxl/v2/program.h>
static int read_trusted(const struct vxl_v2_file_ops *io,const char *path,vxl_u32 max,struct vxl_v2_file_blob *out){
 struct vxl_v2_file_blob b={0};int rc;
 if(!io || !io->read || !io->release || !out)return -EINVAL;
 rc=io->read(io->context,path,max,&b);
 if(!rc && (!b.bytes || !b.length || b.length>max))rc=-EINVAL;
 if(!rc && (b.uid || (b.mode&0022) || !b.regular || !b.no_follow))rc=-EACCES;
 if(rc){if(b.bytes)io->release(io->context,&b);return rc;}
 *out=b;return 0;
}
int vxl_v2_read_catalog_file(const struct vxl_v2_file_ops *io,const struct vxl_v2_provider *p,struct vxl_v2_catalog *cat,struct vxl_v2_file_blob *blob){
 struct vxl_v2_file_blob b={0};int rc;
 if(!p || !cat || !blob)return -EINVAL;
 rc=read_trusted(io,"/lib/firmware/vxl/catalog-v1.bin",65536,&b);if(rc)return rc;
 rc=vxl_v2_catalog_open(cat,b.bytes,b.length,p);
 if(rc){io->release(io->context,&b);return rc;}
 /* The provider retains this private copy until device cleanup/new idle epoch. */
 *blob=b;return 0;
}
int vxl_v2_load_entry_file(struct vxl_v2_ctx *c,vxl_u64 owner,const vxl_u8 id[32],const struct vxl_v2_file_ops *io,struct vxl_v2_program_ref *out){
 const char prefix[]="/lib/firmware/vxl/programs/",hex[]="0123456789abcdef";char path[sizeof(prefix)+64+4];struct vxl_v2_catalog_entry e;struct vxl_v2_file_blob b={0};vxl_u64 end;unsigned int i;int rc;
 if(!c || !c->programs || !id || !out)return -EINVAL;
 rc=vxl_v2_owner_check(c,owner,c->epoch);if(rc)return rc;
 rc=vxl_v2_catalog_lookup(&c->programs->catalog,id,&e);if(rc)return rc;
 memcpy(path,prefix,sizeof(prefix)-1);
 for(i=0;i<32;i++){path[sizeof(prefix)-1+2*i]=hex[id[i]>>4];path[sizeof(prefix)+2*i]=hex[id[i]&15];}
 memcpy(path+sizeof(prefix)-1+64,".bin",5);
 rc=read_trusted(io,path,8388608,&b);if(rc)return rc;
 end=(vxl_u64)e.code_offset+e.code_bytes;
 if((vxl_u64)e.rodata_offset+e.rodata_bytes>end)end=(vxl_u64)e.rodata_offset+e.rodata_bytes;
 if(end!=b.length)rc=-EINVAL;
 else rc=vxl_v2_program_load(c,owner,&e,b.bytes+e.code_offset,e.rodata_bytes?b.bytes+e.rodata_offset:0,out);
 io->release(io->context,&b);return rc;
}
#ifdef __KERNEL__
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/err.h>
#include <linux/uidgid.h>
static int linux_read_owned(void *context,const char *path,vxl_u32 max,struct vxl_v2_file_blob *b){
 struct file *file;struct inode *inode;loff_t pos=0;vxl_u64 size;vxl_u8 *bytes;ssize_t n;int rc=0;(void)context;
 file=filp_open(path,O_RDONLY|O_NOFOLLOW,0);if(IS_ERR(file))return PTR_ERR(file);
 inode=file_inode(file);size=i_size_read(inode);
 if(!S_ISREG(inode->i_mode) || !uid_eq(inode->i_uid,GLOBAL_ROOT_UID) || (inode->i_mode&0022)){rc=-EACCES;goto done;}
 if(!size || size>max){rc=-EINVAL;goto done;}
 bytes=kvmalloc((size_t)size,GFP_KERNEL);if(!bytes){rc=-ENOMEM;goto done;}
 while((vxl_u64)pos<size){n=kernel_read(file,bytes+pos,(size_t)(size-pos),&pos);if(n<=0){rc=n<0?(int)n:-EIO;kvfree(bytes);goto done;}}
 b->bytes=bytes;b->length=(vxl_u32)size;b->uid=0;b->mode=inode->i_mode;b->regular=1;b->no_follow=1;
done:
 filp_close(file,0);return rc;
}
static void linux_release_owned(void *context,struct vxl_v2_file_blob *b){(void)context;kvfree(b->bytes);memset(b,0,sizeof(*b));}
const struct vxl_v2_file_ops vxl_v2_linux_files={0,linux_read_owned,linux_release_owned};
#endif

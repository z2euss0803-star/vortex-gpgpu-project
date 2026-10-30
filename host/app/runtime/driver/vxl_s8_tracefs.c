#include "vxl_internal.h"
#include <linux/debugfs.h>
#include <linux/vmalloc.h>
#include <linux/random.h>
#include <linux/slab.h>
/* Each reader owns immutable serialized bytes. It earns delivery only from
 * contiguous successful user copies, never from seq_show or buffer creation. */
struct s8_export {
 struct vxl_device *device;
 struct vxl_s8_reader reader;
 char *data;
 size_t bytes;
};
struct s8_buffer {char *data;size_t used,capacity;};
static int buffer_write(void *p,const char *data,size_t bytes) {
 struct s8_buffer *b=p;
 if(bytes>4*1024*1024-b->used)return -EOVERFLOW;
 if(b->data){if(bytes>b->capacity-b->used)return -EOVERFLOW;memcpy(b->data+b->used,data,bytes);}
 b->used+=bytes;return 0;
}
static int s8_file_open(struct inode *inode,struct file *file) {
 struct vxl_s8_file_key *key=inode->i_private;struct vxl_device *d=key->device;
 struct vxl_s8_trace *snap;struct s8_export *out;struct s8_buffer b={0};int rc;
 out=kzalloc(sizeof(*out),GFP_KERNEL);snap=kvzalloc(sizeof(*snap),GFP_KERNEL);
 if(!out||!snap){rc=-ENOMEM;goto free;}
 snap->jobs=kvmalloc_array(256,sizeof(*snap->jobs),GFP_KERNEL);snap->operations=kvmalloc_array(2048,sizeof(*snap->operations),GFP_KERNEL);
 if(!snap->jobs||!snap->operations){rc=-ENOMEM;goto free;}
 if(!mutex_trylock(&d->mutex)){rc=-EBUSY;goto free;}
 vxl_s8_trace_sync(&d->s8);
 {struct vxl_s8_job_row *jobs=snap->jobs;struct vxl_s8_op_row *ops=snap->operations;
  *snap=d->s8_trace;snap->jobs=jobs;snap->operations=ops;
  memcpy(jobs,d->s8_trace.jobs,snap->jobs_used*sizeof(*jobs));memcpy(ops,d->s8_trace.operations,snap->operations_used*sizeof(*ops));}
 mutex_unlock(&d->mutex);
 rc=vxl_s8_trace_emit(snap,d->s8.program,key->file,buffer_write,&b);if(rc)goto free;
 out->data=kvmalloc(b.used,GFP_KERNEL);if(!out->data){rc=-ENOMEM;goto free;}
 out->bytes=b.used;b.data=out->data;b.capacity=b.used;b.used=0;
 rc=vxl_s8_trace_emit(snap,d->s8.program,key->file,buffer_write,&b);if(rc)goto free;
 mutex_lock(&d->mutex);
 if(d->s8_trace.revision!=snap->revision||d->s8_trace.generation!=snap->generation)rc=-ESTALE;
 else rc=vxl_s8_reader_begin(&d->s8,key->file,out->bytes,&out->reader);
 mutex_unlock(&d->mutex);
 if(rc)goto free;
 out->device=d;file->private_data=out;
 kvfree(snap->operations);kvfree(snap->jobs);kvfree(snap);return 0;
free:
 if(snap){kvfree(snap->operations);kvfree(snap->jobs);kvfree(snap);}
 if(out){kvfree(out->data);kfree(out);}return rc;
}
static ssize_t s8_file_read(struct file *file,char __user *buffer,size_t bytes,loff_t *offset) {
 struct s8_export *out=file->private_data;struct vxl_device *d=out->device;size_t n;int rc,fault;
 if(*offset<0||(u64)*offset>out->bytes)return -EINVAL;
 if(!bytes)return 0;
 if((u64)*offset==out->bytes)return 0;
 n=out->bytes-(size_t)*offset;if(n>bytes)n=bytes;
 fault=copy_to_user(buffer,out->data+*offset,n)!=0;
 mutex_lock(&d->mutex);
 rc=vxl_s8_reader_advance(&d->s8,&out->reader,*offset,n,!fault);
 if(!rc&&out->reader.delivered==out->reader.bytes)rc=vxl_s8_reader_finish(&d->s8,&out->reader);
 mutex_unlock(&d->mutex);
 /* Forensic bytes from an active/stale snapshot remain readable. They do
  * not earn delivery, so an observer can keep evidence without a false REAP. */
 if(fault)return -EFAULT;
 if(rc && rc!=-ESTALE && rc!=-EBUSY)return rc;
 *offset+=n;return n;
}
static int s8_file_release(struct inode *inode,struct file *file) {
 struct s8_export *out=file->private_data;(void)inode;kvfree(out->data);kfree(out);return 0;
}
static const struct file_operations s8_fops={.owner=THIS_MODULE,.open=s8_file_open,.read=s8_file_read,.release=s8_file_release,.llseek=no_llseek};
static void s8_remove(void *p){debugfs_remove_recursive(p);}
static void s8_rows_free(void *p){struct vxl_s8_trace *t=p;kvfree(t->operations);kvfree(t->jobs);}
int vxl_s8_tracefs_init(struct vxl_device *d) {
 struct vxl_s8_job_row *jobs;struct vxl_s8_op_row *ops;struct dentry *dir,*file;u8 boot[16];int rc;
 static const char *const names[]={"binding.bin","jobs.csv","operations.csv"};
 jobs=kvcalloc(256,sizeof(*jobs),GFP_KERNEL);ops=kvcalloc(2048,sizeof(*ops),GFP_KERNEL);
 if(!jobs||!ops){kvfree(jobs);kvfree(ops);return -ENOMEM;}
 get_random_bytes(boot,16);if(!memchr_inv(boot,0,16)){kvfree(jobs);kvfree(ops);return -EIO;}
 rc=vxl_s8_trace_init(&d->s8,&d->s8_trace,jobs,ops,boot);if(rc){kvfree(jobs);kvfree(ops);return rc;}
 rc=devm_add_action_or_reset(d->dev,s8_rows_free,&d->s8_trace);if(rc)return rc;
 dir=debugfs_create_dir("vxl_s8",NULL);if(IS_ERR_OR_NULL(dir))return dir?PTR_ERR(dir):-ENOMEM;
 for(unsigned i=0;i<3;++i){d->s8_files[i].device=d;d->s8_files[i].file=i;file=debugfs_create_file(names[i],0400,dir,&d->s8_files[i],&s8_fops);if(IS_ERR_OR_NULL(file)){rc=file?PTR_ERR(file):-ENOMEM;debugfs_remove_recursive(dir);return rc;}}
 return devm_add_action_or_reset(d->dev,s8_remove,dir);
}

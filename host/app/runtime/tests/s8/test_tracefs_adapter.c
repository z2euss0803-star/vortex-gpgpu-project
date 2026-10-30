/* Production debugfs open/read/release, tested at allocation/copy/mutex edges.
 * This is not a running Linux debugfs filesystem or a board result. */
#include "state_fixture.h"
#include <vxl/s8_trace.h>
#include <stdio.h>
#include <sys/types.h>
typedef vxl_u64 u64;typedef vxl_u8 u8;
#define loff_t s8_test_loff_t
typedef int64_t loff_t;
#define VXL_INTERNAL_H
#define __user
#define THIS_MODULE ((void *)0)
#define no_llseek ((void *)0)
#define GFP_KERNEL 0
#define IS_ERR_OR_NULL(p) (!(p))
#define PTR_ERR(p) (-ENOMEM)
struct file {void *private_data;};struct inode {void *i_private;};struct mutex {int unused;};struct dentry {int unused;};
struct file_operations {void *owner;int (*open)(struct inode *,struct file *);ssize_t (*read)(struct file *,char *,size_t,loff_t *);int (*release)(struct inode *,struct file *);void *llseek;};
struct vxl_device;
struct vxl_s8_file_key {struct vxl_device *device;unsigned file;};
struct vxl_device {struct mutex mutex;struct vxl_engine engine;struct vxl_s8_ctx s8;struct vxl_s8_trace s8_trace;struct vxl_s8_file_key s8_files[3];void *dev;};
static int fault,allocation_fault;
static void *kzalloc(size_t n,int f){(void)f;return allocation_fault?NULL:calloc(1,n);}
static void *kvzalloc(size_t n,int f){return kzalloc(n,f);}
static void *kvmalloc(size_t n,int f){(void)f;return allocation_fault?NULL:malloc(n);}
static void *kvmalloc_array(size_t n,size_t s,int f){return kvmalloc(n*s,f);}
static void *kvcalloc(size_t n,size_t s,int f){return kvzalloc(n*s,f);}
static void kvfree(void *p){free(p);}static void kfree(void *p){free(p);}
static void mutex_lock(struct mutex *p){(void)p;}static void mutex_unlock(struct mutex *p){(void)p;}static int mutex_trylock(struct mutex *p){(void)p;return 1;}
static int copy_to_user(void *dst,const void *src,size_t n){if(fault)return 1;memcpy(dst,src,n);return 0;}
static void get_random_bytes(void *p,size_t n){memset(p,9,n);}
static void *memchr_inv(const void *p,int c,size_t n){const unsigned char *b=p;for(size_t i=0;i<n;++i)if(b[i]!=(unsigned char)c)return (void *)(b+i);return NULL;}
static struct dentry dummy;
static struct dentry *debugfs_create_dir(const char *n,void *p){(void)n;(void)p;return &dummy;}
static struct dentry *debugfs_create_file(const char *n,unsigned mode,struct dentry *p,void *key,const struct file_operations *ops){(void)n;(void)p;(void)key;assert(mode==0400 && ops->llseek==no_llseek);return &dummy;}
static void debugfs_remove_recursive(void *p){(void)p;}
static int devm_add_action_or_reset(void *d,void (*f)(void *),void *p){(void)d;(void)f;(void)p;return 0;}
#include "../../driver/vxl_s8_tracefs.c"
int main(void){
 struct state_fixture *s=calloc(1,sizeof(*s));struct vxl_device *d=calloc(1,sizeof(*d));struct vxl_s8_program p={0};assert(s&&d);state_init(s);state_complete(s);
 d->engine=s->m.engine;d->s8=s->c;d->s8.engine=&d->engine;d->engine.s8=&d->s8;d->s8.program=&p;
 assert(!vxl_s8_tracefs_init(d));struct vxl_s8_hw_snapshot hw=d->s8.hw;
 assert(!vxl_s8_trace_begin(&d->s8,&d->s8.job_args,1));d->s8.hw=hw;vxl_s8_trace_sync(&d->s8);
 struct inode inode={.i_private=&d->s8_files[1]};struct file f={0};char buffer[4096];loff_t offset=0;
 assert(!s8_file_open(&inode,&f));assert(s8_file_read(&f,buffer,17,&offset)==17 && !d->s8.trace_delivered);
 fault=1;assert(s8_file_read(&f,buffer,17,&offset)==-EFAULT && offset==17 && !d->s8.trace_delivered);fault=0;
 assert(s8_file_read(&f,buffer,17,&offset)==-EINVAL);assert(!s8_file_release(&inode,&f));
 assert(!s8_file_open(&inode,&f));offset=1;assert(s8_file_read(&f,buffer,17,&offset)==-EINVAL && !d->s8.trace_delivered);assert(!s8_file_release(&inode,&f));
 assert(!s8_file_open(&inode,&f));struct vxl_s8_op_row op={.metric=1};assert(!vxl_s8_trace_append(&d->s8,1,&op,sizeof(op)));
 offset=0;ssize_t n;do{n=s8_file_read(&f,buffer,sizeof(buffer),&offset);assert(n>=0);}while(n);assert(!d->s8.trace_delivered);assert(!s8_file_release(&inode,&f));
 for(unsigned i=0;i<3;++i){inode.i_private=&d->s8_files[i];assert(!s8_file_open(&inode,&f));offset=0;do{n=s8_file_read(&f,buffer,101,&offset);assert(n>=0);}while(n);assert(!s8_file_release(&inode,&f));}
 assert(d->s8.trace_delivered==7 && !vxl_s8_trace_open_allowed(&d->s8));
 allocation_fault=1;vxl_u64 revision=d->s8_trace.revision;assert(s8_file_open(&inode,&f)==-ENOMEM && revision==d->s8_trace.revision);allocation_fault=0;
 s8_rows_free(&d->s8_trace);state_destroy(s);free(d);free(s);
 puts("S8_REAL_TRACEFS_COPY_FAULT_SEEK_STALE_FRAGMENTED_DELIVERY_PASS SOFTWARE_ONLY");return 0;
}

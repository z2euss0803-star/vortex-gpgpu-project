#include "vxl_internal.h"
#include <linux/debugfs.h>
#include <linux/seq_file.h>

static int trace_show(struct seq_file *out,void *unused){
 struct vxl_device *d=out->private;unsigned i;int rc=0;(void)unused;
 mutex_lock(&d->mutex);
 if(d->submit_trace.active){rc=-EBUSY;goto done;}
 seq_printf(out,"#VXL_SUBMIT_TIMING_V2 capacity=%u used=%u flags=%u active=0\n",d->submit_trace.capacity,d->submit_trace.used,d->submit_trace.flags);
#define TRACE_HEADER(n) seq_puts(out,#n ",");
 VXL_TIME_FIELDS(TRACE_HEADER)
#undef TRACE_HEADER
 seq_puts(out,"guard_depth\n");
 for(i=0;i<d->submit_trace.used;i++){
  struct vxl_submit_time *t=&d->submit_trace.rows[i];
#define TRACE_VALUE(n) seq_printf(out,"%llu,",(unsigned long long)t->n);
  VXL_TIME_FIELDS(TRACE_VALUE)
#undef TRACE_VALUE
  seq_printf(out,"%u\n",t->guard_depth);
 }
done:mutex_unlock(&d->mutex);return rc;
}
static int trace_open(struct inode *inode,struct file *file){return single_open(file,trace_show,inode->i_private);}
static const struct file_operations trace_fops={.owner=THIS_MODULE,.open=trace_open,.read=seq_read,.llseek=seq_lseek,.release=single_release};
static void trace_remove(void *p){debugfs_remove_recursive(p);}
int vxl_submit_trace_init(struct vxl_device *d){
 struct dentry *dir,*file;
 d->submit_trace.capacity=2048;
 d->submit_trace.rows=devm_kcalloc(d->dev,d->submit_trace.capacity,sizeof(*d->submit_trace.rows),GFP_KERNEL);
 if(!d->submit_trace.rows)return -ENOMEM;
 dir=debugfs_create_dir("vxl_submit_timing",NULL);if(IS_ERR(dir))return PTR_ERR(dir);if(!dir)return -ENOMEM;
 file=debugfs_create_file("jobs.csv",0400,dir,d,&trace_fops);
 if(IS_ERR_OR_NULL(file)){int rc=file?PTR_ERR(file):-ENOMEM;debugfs_remove_recursive(dir);return rc;}
 return devm_add_action_or_reset(d->dev,trace_remove,dir);
}

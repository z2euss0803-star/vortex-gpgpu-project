#include "vxl_internal.h"
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/vmalloc.h>
static int write_csv(void *ctx,const char *data,size_t n){return seq_write(ctx,data,n);}
static int mgmt_show(struct seq_file *out,void *unused){
 struct vxl_device *d=out->private;struct vxl_mgmt_trace snap;struct vxl_mgmt_row *rows;int rc;(void)unused;
 rows=kvmalloc_array(VXL_MGMT_CAPACITY,sizeof(*rows),GFP_KERNEL);if(!rows)return -ENOMEM;
 /* Never wait behind a long DMA transaction to print diagnostics. */
 if(!mutex_trylock(&d->mutex)){kvfree(rows);return -EBUSY;}
 snap=d->mgmt_trace;
 if(snap.active||snap.used>VXL_MGMT_CAPACITY){mutex_unlock(&d->mutex);kvfree(rows);return -EBUSY;}
 if(snap.used)memcpy(rows,snap.rows,snap.used*sizeof(*rows));
 snap.rows=rows;mutex_unlock(&d->mutex);
 rc=vxl_mgmt_emit(&snap,write_csv,out);
 if(!rc){mutex_lock(&d->mutex);if(!d->mgmt_trace.active&&d->mgmt_trace.used==snap.used)d->mgmt_trace.exported_used=snap.used;mutex_unlock(&d->mutex);}
 kvfree(rows);return rc;
}
static int mgmt_open(struct inode *inode,struct file *file){return single_open(file,mgmt_show,inode->i_private);}
static const struct file_operations mgmt_fops={.owner=THIS_MODULE,.open=mgmt_open,.read=seq_read,.llseek=seq_lseek,.release=single_release};
static void mgmt_remove(void *p){debugfs_remove_recursive(p);}
static int digest_show(struct seq_file *out,void *unused){
 struct vxl_device *d=out->private;struct vxl_digest_audit audit;unsigned i;u32 generation;(void)unused;
 if(!mutex_trylock(&d->mutex))return -EBUSY;
 audit=d->hunet.opt.digest_audit;generation=d->engine.generation;mutex_unlock(&d->mutex);
 seq_printf(out,"generation,backend,implementation,calls,bytes,failures,fallbacks,overflow\n");
 for(i=0;i<4;i++)if(audit.backend[i].calls){const struct vxl_digest_audit_row *r=&audit.backend[i];seq_printf(out,"%u,%u,%s,%llu,%llu,%llu,%llu,%u\n",generation,i,r->implementation,(unsigned long long)r->calls,(unsigned long long)r->bytes,(unsigned long long)r->failures,(unsigned long long)r->fallbacks,audit.overflow);}
 return 0;
}
static int digest_open(struct inode *inode,struct file *file){return single_open(file,digest_show,inode->i_private);}
static const struct file_operations digest_fops={.owner=THIS_MODULE,.open=digest_open,.read=seq_read,.llseek=seq_lseek,.release=single_release};
int vxl_mgmt_trace_init(struct vxl_device *d){
 struct dentry *dir,*file;
 d->mgmt_trace.capacity=VXL_MGMT_CAPACITY;
 d->mgmt_trace.rows=devm_kcalloc(d->dev,VXL_MGMT_CAPACITY,sizeof(*d->mgmt_trace.rows),GFP_KERNEL);
 if(!d->mgmt_trace.rows){d->mgmt_trace.flags|=VXL_MGMT_INCOMPLETE;return -ENOMEM;}
 dir=debugfs_create_dir("vxl_mgmt_timing",NULL);if(IS_ERR(dir))return PTR_ERR(dir);if(!dir)return -ENOMEM;
 file=debugfs_create_file("transactions.csv",0400,dir,d,&mgmt_fops);
 if(IS_ERR_OR_NULL(file)){int rc=file?PTR_ERR(file):-ENOMEM;debugfs_remove_recursive(dir);return rc;}
 file=debugfs_create_file("digest.csv",0400,dir,d,&digest_fops);
 if(IS_ERR_OR_NULL(file)){int rc=file?PTR_ERR(file):-ENOMEM;debugfs_remove_recursive(dir);return rc;}
 return devm_add_action_or_reset(d->dev,mgmt_remove,dir);
}

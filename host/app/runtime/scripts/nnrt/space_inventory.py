"""Read-only exact-path capacity facts, without broad recursive scanning."""
import os
from pathlib import Path
import shutil


def inventory(paths):
    records=[]
    for name in paths:
        p=Path(name).absolute()
        row={'path':str(p),'exists':p.exists(),'gpu_raw_cycles':None}
        try:
            st=p.lstat();row.update(owner_uid=getattr(st,'st_uid',None),link=p.is_symlink(),
                                   logical_bytes=st.st_size if p.is_file() else None,
                                   allocated_bytes=getattr(st,'st_blocks',0)*512 if hasattr(st,'st_blocks') else None,
                                   free_bytes=shutil.disk_usage(str(p)).free,
                                   children=sorted(x.name for x in p.iterdir()) if p.is_dir() and not p.is_symlink() else None,
                                   device=st.st_dev)
        except OSError as error:row['error']=str(error)
        records.append(row)
    return records

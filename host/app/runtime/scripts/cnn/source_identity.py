"""Read the real worktree identity with its native Git; never rewrite .git."""
import os
import re
import shutil
import subprocess
from pathlib import Path

def git_bytes(root,*args):
    root=Path(root).resolve();native=str(root);executable='git'
    marker=root/'.git'
    if os.name!='nt' and marker.is_file() and re.match(r'gitdir: [A-Za-z]:[/\\]',marker.read_text(encoding='utf-8')):
        executable=shutil.which('git.exe') or '/mnt/f/git/Git/cmd/git.exe'
        if not Path(executable).is_file():raise FileNotFoundError('Windows Git required for Windows worktree metadata')
        native=subprocess.check_output(['wslpath','-w',str(root)],text=True).strip().replace('\\','/')
    return subprocess.check_output([executable,'-c','safe.directory='+native,'-C',native,*args],cwd=root)

def source_commit(root):
    root=Path(root)
    if (root/'.git').exists():return git_bytes(root,'rev-parse','HEAD').decode('utf-8').strip()
    return (root/'SOURCE_COMMIT').read_text(encoding='utf-8').strip()

def source_tree_clean(root):
    try:
        git_bytes(root,'diff','--quiet','HEAD','--')
        return True
    except subprocess.CalledProcessError as e:
        if e.returncode==1:return False
        raise

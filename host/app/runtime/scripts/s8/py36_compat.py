"""Host-test API compatibility for Ubuntu18 Python3.6, never a target runtime.

Only keyword/method adapters; original legacy source and assertions stay intact.
Installed per process by private PYTHONPATH, never into system site-packages.
"""
import pathlib,shutil,subprocess,sys

def timespec_ns(seconds,nanoseconds):
    if type(seconds) is not int or type(nanoseconds) is not int or not 0<=nanoseconds<1000000000:raise ValueError('invalid native timespec')
    return seconds*1000000000+nanoseconds
def translate_run(kwargs):
    k=dict(kwargs)
    if k.pop('capture_output',False):
        if 'stdout' in k or 'stderr' in k:raise ValueError('stdout/stderr with capture_output')
        k.update(stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    return k
def translate_text(kwargs):
    k=dict(kwargs)
    if 'text' in k:
        text=k.pop('text')
        if 'universal_newlines' in k and bool(k['universal_newlines'])!=bool(text):raise subprocess.SubprocessError('Cannot disambiguate text and universal_newlines')
        k['universal_newlines']=text
    return k
def install():
    if sys.version_info>=(3,7):return
    old_run=subprocess.run;old_init=subprocess.Popen.__init__
    def run(*args,**kwargs):return old_run(*args,**translate_text(translate_run(kwargs)))
    def init(self,*args,**kwargs):return old_init(self,*args,**translate_text(kwargs))
    subprocess.run=run;subprocess.Popen.__init__=init
    import time,ctypes
    if not hasattr(time,'monotonic_ns'):
        class Timespec(ctypes.Structure):_fields_=[('seconds',ctypes.c_long),('nanoseconds',ctypes.c_long)]
        libc=ctypes.CDLL(None,use_errno=True);clock=libc.clock_gettime
        clock.argtypes=[ctypes.c_int,ctypes.POINTER(Timespec)];clock.restype=ctypes.c_int
        def monotonic_ns():
            value=Timespec()
            if clock(time.CLOCK_MONOTONIC,ctypes.byref(value)):raise OSError(ctypes.get_errno(),'clock_gettime')
            return timespec_ns(value.seconds,value.nanoseconds)
        time.monotonic_ns=monotonic_ns
    old_write=pathlib.Path.write_text
    def write_text(self,data,encoding=None,errors=None,**kwargs):
        if not kwargs:return old_write(self,data,encoding,errors)
        newline=kwargs.pop('newline',None)
        if kwargs:raise TypeError('unsupported write_text keyword')
        with self.open('w',encoding=encoding,errors=errors,newline=newline) as stream:return stream.write(data)
    pathlib.Path.write_text=write_text
    if not hasattr(pathlib.Path,'is_relative_to'):
        def relative(self,*other):
            try:self.relative_to(*other);return True
            except ValueError:return False
        pathlib.Path.is_relative_to=relative
    old_unlink=pathlib.Path.unlink
    def unlink(self,missing_ok=False):
        try:return old_unlink(self)
        except FileNotFoundError:
            if not missing_ok:raise
    pathlib.Path.unlink=unlink
    old_tree=shutil.copytree
    def copytree(src,dst,symlinks=False,ignore=None,copy_function=shutil.copy2,ignore_dangling_symlinks=False,dirs_exist_ok=False):
        if not dirs_exist_ok:return old_tree(src,dst,symlinks,ignore,copy_function,ignore_dangling_symlinks)
        import os
        os.makedirs(dst,exist_ok=True);names=os.listdir(src);ignored=set(ignore(src,names) if ignore else [])
        for name in names:
            if name in ignored:continue
            a=os.path.join(src,name);b=os.path.join(dst,name)
            if os.path.islink(a) and symlinks:os.symlink(os.readlink(a),b)
            elif os.path.isdir(a):copytree(a,b,symlinks,ignore,copy_function,ignore_dangling_symlinks,True)
            elif not (ignore_dangling_symlinks and os.path.islink(a) and not os.path.exists(a)):copy_function(a,b)
        shutil.copystat(src,dst);return dst
    shutil.copytree=copytree

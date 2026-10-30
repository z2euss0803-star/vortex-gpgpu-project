import hashlib
import io
import os
from pathlib import Path
import tarfile
import tempfile
import unittest
from scripts.nnrt.archive_manifest import verify_archive
from scripts.nnrt.release_verified_cache import release_verified_cache


class ArchiveReleaseTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(dir=str(Path(__file__).resolve().parents[2]/'build'))
        self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name);self.source=self.root/'exclusive-cache';self.source.mkdir()
        (self.source/'a.bin').write_bytes(b'original cache')
        self.archive=self.root/'archive.tar'
        self.manifest={'source_root':str(self.source.resolve()),'files':[{'path':'a.bin','bytes':14,'sha256':hashlib.sha256(b'original cache').hexdigest()}]}
        self.policy={'source_root':str(self.source.resolve()),'paths':['a.bin'],'approved':True,'exclusive_rebuildable':True,'dependencies':[],'active':False,'lock_files':[]}
        self.pack()

    def pack(self, members=None):
        with tarfile.open(self.archive,'w') as tf:
            for name,data in (members if members is not None else [('a.bin',b'original cache')]):
                entry=tarfile.TarInfo(name);entry.size=len(data);tf.addfile(entry,io.BytesIO(data))

    def refuse_release(self):
        receipt=verify_archive(self.archive,self.manifest)
        with self.assertRaises(ValueError):release_verified_cache(self.source,receipt,self.policy,dry_run=False)
        self.assertTrue((self.source/'a.bin').is_file())

    def test_truncated_archive_refused(self):
        self.archive.write_bytes(self.archive.read_bytes()[:520])
        with self.assertRaises(ValueError):verify_archive(self.archive,self.manifest)

    def test_missing_member_refused(self):
        self.pack([])
        with self.assertRaises(ValueError):verify_archive(self.archive,self.manifest)

    def test_same_name_archive_content_drift_refused(self):
        self.pack([('a.bin',b'different data')])
        with self.assertRaises(ValueError):verify_archive(self.archive,self.manifest)

    def test_archive_escape_refused(self):
        self.pack([('../escape',b'original cache')])
        with self.assertRaises(ValueError):verify_archive(self.archive,self.manifest)

    def test_archive_link_refused(self):
        with tarfile.open(self.archive,'w') as tf:
            entry=tarfile.TarInfo('a.bin');entry.type=tarfile.SYMTYPE;entry.linkname='../outside';tf.addfile(entry)
        with self.assertRaises(ValueError):verify_archive(self.archive,self.manifest)

    def test_source_added_file_refused(self):
        (self.source/'unlisted').write_bytes(b'keep');self.refuse_release()
        self.assertEqual((self.source/'unlisted').read_bytes(),b'keep')

    def test_source_content_drift_refused(self):
        (self.source/'a.bin').write_bytes(b'changed cache!');self.refuse_release()

    def test_source_link_escape_refused(self):
        outside=self.root/'outside';outside.mkdir();(outside/'valuable').write_bytes(b'keep')
        try:os.symlink(str(outside),str(self.source/'redirect'),target_is_directory=True)
        except OSError as error:self.skipTest('host symlink privilege unavailable: '+str(error))
        self.refuse_release();self.assertEqual((outside/'valuable').read_bytes(),b'keep')

    def test_archive_duplicate_refused(self):
        self.pack([('a.bin',b'original cache'),('a.bin',b'original cache')])
        with self.assertRaises(ValueError):verify_archive(self.archive,self.manifest)

    def test_archive_changed_after_receipt_refused(self):
        receipt=verify_archive(self.archive,self.manifest);self.pack([('a.bin',b'different data')])
        with self.assertRaises(ValueError):release_verified_cache(self.source,receipt,self.policy,dry_run=False)
        self.assertTrue((self.source/'a.bin').is_file())

    def test_shared_dependency_refused(self):
        self.policy['dependencies']=['C graphics'];self.refuse_release()

    def test_active_task_refused(self):
        self.policy['active']=True;self.refuse_release()

    @unittest.skipIf(os.name=='nt','Linux advisory lock semantics exercised on Ubuntu')
    def test_active_lock_refused(self):
        import fcntl
        lock=self.root/'owner.lock'
        with lock.open('a') as stream:
            fcntl.flock(stream,fcntl.LOCK_EX|fcntl.LOCK_NB)
            self.policy['lock_files']=[str(lock)];self.refuse_release()

    def test_dry_run_default_and_exact_release(self):
        receipt=verify_archive(self.archive,self.manifest)
        report=release_verified_cache(self.source,receipt,self.policy)
        self.assertTrue(report['dry_run']);self.assertTrue((self.source/'a.bin').exists())
        report=release_verified_cache(self.source,receipt,self.policy,dry_run=False)
        self.assertEqual(report['released'],['a.bin']);self.assertFalse((self.source/'a.bin').exists())
        self.assertTrue(self.source.is_dir());self.assertTrue(self.archive.is_file())


if __name__=='__main__':unittest.main()

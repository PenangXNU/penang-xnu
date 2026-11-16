# SPDX-License-Identifier: Apache-2.0
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/"tools/m12-penang-physical"))
import p1_link_inputs as links


class LinkInputs(unittest.TestCase):
    def setUp(self):
        self.cc=shutil.which("clang")
        self.nm=shutil.which("llvm-nm")
        if not self.cc or not self.nm: self.skipTest("Clang and llvm-nm required")
        self.temp=tempfile.TemporaryDirectory(prefix="p1-link-input-")
        self.root=Path(self.temp.name)

    def tearDown(self):
        if hasattr(self,"temp"): self.temp.cleanup()

    def obj(self,name,body,identity=None):
        source=self.root/(name+".c")
        source.write_text(body)
        obj=self.root/(name+".o")
        subprocess.run([self.cc,"--target=arm64-apple-darwin","-fno-common","-c",str(source),"-o",str(obj)],check=True,capture_output=True)
        return {"source_identity":identity or "osfmk/"+name+".c","source_sha256":hashlib.sha256(source.read_bytes()).hexdigest(),"object":str(obj)}

    def test_exact_alias_is_coalesced_with_both_hashes_retained(self):
        row=self.obj("memory","int p1_memory_admit(void) { return 7; }\n")
        alias=self.root/"alias.o"
        shutil.copy(row["object"],alias)
        result=links.select([row,{**row,"object":str(alias)}],self.nm)
        self.assertEqual((len(result['selected']),len(result['exact_source_aliases'])),(1,1))
        self.assertIn('_p1_memory_admit',result['selected'][0]['strong'])
        self.assertEqual(result['exact_source_aliases'][0]['object_sha256'],result['selected'][0]['object_sha256'])

    def test_duplicate_strong_provider_and_conflicting_alias_are_errors(self):
        a=self.obj("first","void pe_init_fiq(void) {}\n")
        b=self.obj("second","void pe_init_fiq(void) { __asm__ volatile(\"nop\"); }\n")
        self.assertRaisesRegex(ValueError,'duplicate strong provider',links.select,[a,b],self.nm)
        self.assertRaisesRegex(ValueError,'conflicting repeated',links.select,[a,{**b,'source_identity':a['source_identity']}],self.nm)
        self.assertRaisesRegex(ValueError,'conflicting repeated',links.select,[a,{**a,'source_sha256':'0'*64}],self.nm)

    def test_empty_objects_weak_symbols_and_missing_provider_are_classified(self):
        empty=self.obj('empty','static int unused;\n')
        a=self.obj('weak1','__attribute__((weak)) int f(void) { return 1; }\n')
        b=self.obj('weak2','__attribute__((weak)) int f(void) { return 2; }\n')
        need=self.obj('consumer','extern int provider(void); int need(void) { return provider(); }\n')
        result=links.select([empty,a,b,need],self.nm)
        self.assertEqual((result['nonempty_objects'],result['empty_nonproviders']),(3,1))
        self.assertIn('_provider',result['selected'][-1]['undefined'])
        self.assertEqual(result['link'],'NOT_RUN')

    def test_architecture_paths_symlinks_and_invalid_identity_are_rejected(self):
        row=self.obj('valid','int defined;\n')
        alias=self.root/'alias.o';alias.symlink_to(row['object'])
        self.assertRaisesRegex(ValueError,'symlink',links.select,[{**row,'object':str(alias)}],self.nm)
        for identity in ['../escape.c','/absolute.c','osfmk/./file.c']:
            self.assertRaises(ValueError,links.select,[{**row,'source_identity':identity}],self.nm)
        for bad in [[],[None],["invalid"],[{**row,"object":None}],[{**row,"object":""}]]:
            self.assertRaises(ValueError,links.select,bad,self.nm)
        p=Path(row['object']);data=p.read_bytes()
        # arm64e subtype instead of admitted arm64-all.
        p.write_bytes(data[:8]+b'\x02\0\0\0'+data[12:])
        self.assertRaisesRegex(ValueError,'MH_OBJECT',links.select,[row],self.nm)
        p.write_bytes(b'\x7fELF'+b'\0'*40)
        self.assertRaisesRegex(ValueError,'MH_OBJECT',links.select,[row],self.nm)


if __name__=='__main__': unittest.main()

# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
MEMORY=ROOT/"tools/m12-penang-physical"
PLATFORM=ROOT/"tools/p1-penang-platform"
sys.path.insert(0,str(MEMORY))
import p1_runtime_fdt_consumer as consumer


def tree():
    strings=b"#address-cells\0#size-cells\0device_type\0reg\0"
    def word(x): return struct.pack(">I",x)
    def node(s):
        b=s.encode()+b"\0"
        return word(1)+b+b"\0"*((-len(b))%4)
    def prop(name,value):
        return word(3)+word(len(value))+word(strings.index(name.encode()+b"\0"))+value+b"\0"*((-len(value))%4)
    body=node("")+prop("#address-cells",word(2))+prop("#size-cells",word(2))
    body+=node("memory@80000000")+prop("device_type",b"memory\0")
    body+=prop("reg",struct.pack(">QQ",0x80000000,0x4000000))+word(2)+word(2)+word(9)
    return struct.pack(">10I",0xd00dfeed,56+len(body)+len(strings),56,56+len(body),40,17,16,0,len(strings),len(body))+b"\0"*16+body+strings


class RuntimeFdtAbi(unittest.TestCase):
    def setUp(self):
        self.cc=shutil.which("clang")
        if not self.cc: self.skipTest("Clang required")
        self.temp=tempfile.TemporaryDirectory(prefix="p1-fdt-abi-")
        self.root=Path(self.temp.name)

    def tearDown(self):
        if hasattr(self,"temp"): self.temp.cleanup()

    def test_mutable_storage_const_view_type_in_c_and_cpp(self):
        for lang,check in [("c","_Static_assert(_Generic(&penang_p1_runtime_fdt, struct p1_runtime_fdt *:1, default:0), \"mutable storage\");"),
                           ("c++","static_assert(__is_same(decltype(&penang_p1_runtime_fdt), struct p1_runtime_fdt *));")]:
            with self.subTest(lang=lang):
                source=self.root/("type.c" if lang=="c" else "type.cpp")
                source.write_text('#include "p1_runtime_fdt_provider.h"\n'+check+'\nconst struct p1_runtime_fdt *view(void) { return p1_runtime_fdt_view(); }\n')
                r=subprocess.run([self.cc,"-x",lang,"-I",str(MEMORY),"-Werror","-c",str(source),"-o",str(self.root/"type.o")],capture_output=True,text=True)
                self.assertEqual(r.returncode,0,r.stderr)
        source.write_text('#include "p1_runtime_fdt_provider.h"\nextern const struct p1_runtime_fdt penang_p1_runtime_fdt;\n')
        r=subprocess.run([self.cc,"-I",str(MEMORY),"-c",str(source),"-o",str(self.root/"bad.o")],capture_output=True,text=True)
        self.assertNotEqual(r.returncode,0,"old const object declaration must conflict")
        source.write_text('#include "p1_runtime_fdt_provider.h"\nvoid f(void) { p1_runtime_fdt_view()->length=4; }\n')
        r=subprocess.run([self.cc,"-I",str(MEMORY),"-c",str(source),"-o",str(self.root/"bad.o")],capture_output=True,text=True)
        self.assertNotEqual(r.returncode,0,"consumer view must remain read-only")

    def test_consumer_delta_preserves_guard_and_rejects_source_drift(self):
        guard='#if defined(PENANG_P2_PROTECTED)\n#error B42_BLOCKED\n#endif\n'
        source=guard+consumer.OLD+'int f(void) { return '+consumer.CALL+'0,0,0); }\n'
        changed=consumer.rewrite(source)
        self.assertTrue(changed.startswith(guard))
        self.assertNotIn('extern const struct',changed)
        self.assertIn('p1_runtime_fdt_view()',changed)
        for bad in [source+consumer.OLD,source+consumer.CALL,source.replace(consumer.CALL,'other(')]:
            self.assertRaises(ValueError,consumer.rewrite,bad)
        path=self.root/consumer.SOURCE
        path.parent.mkdir(parents=True)
        path.write_text(source)
        self.assertRaisesRegex(ValueError,'SHA mismatch',consumer.render,self.root)
        path.unlink()
        elsewhere=self.root/'elsewhere.c'
        elsewhere.write_text(source)
        path.symlink_to(elsewhere)
        self.assertRaisesRegex(ValueError,'symlink',consumer.render,self.root)

    def test_real_provider_unbound_bind_lifetime_and_missing_storage(self):
        payload=",".join(str(b) for b in tree())
        source=self.root/"consumer.c"
        source.write_text('''#include "p1_runtime_fdt_provider.h"
#include "ppp_runtime_fdt.h"
#include <stdint.h>
_Alignas(16384) static unsigned char fdt[] = {'''+payload+'''};
static uint64_t virt;
/* ABI 2: mapping evidence. Only the fixture's own pages are vouched for. */
static int translate(void *ctx, uint64_t va, uint64_t *out) {
    (void)ctx;
    if (va < (uintptr_t)fdt || va >= (uintptr_t)fdt + sizeof(fdt)) return 1;
    *out = 0x80000000 + (va - virt);
    return 0;
}
int main(void) {
    const uint64_t base=0x80000000, top=0x80008000, pa=base+0x4000;
    if (p1_memory_admit(p1_runtime_fdt_view(),base,0x4000000,top)!=P1_MEMORY_BAD_FDT) return 1;
    virt=(uintptr_t)fdt-0x4000;
    struct ppp_fdt_handoff h={PPP_FDT_ABI_VERSION,0,pa,sizeof(fdt),base,virt,0x4000000,
                              pa /* kernel end */,top,base /* ADT */,0x1000,base+0x2000 /* boot_args */};
    struct ppp_fdt_mapping m={0,translate,virt,virt+0x4000000};
    h.fdt_length--;
    if (ppp_runtime_fdt_bind(&h,&m)!=PPP_FDT_E_LENGTH || p1_runtime_fdt_view()->bytes) return 2;
    h.fdt_length++;
    if (ppp_runtime_fdt_bind(&h,&m)!=PPP_FDT_OK) return 3;
    if (p1_runtime_fdt_view()->bytes!=fdt || p1_runtime_fdt_view()->physical_address!=pa) return 4;
    if (p1_memory_admit(p1_runtime_fdt_view(),base,0x4000000,top)!=P1_MEMORY_GEOMETRY_OK) return 5;
    if (ppp_runtime_fdt_bind(&h,&m)!=PPP_FDT_E_ALREADY_BOUND) return 6;
    return 0;
}
''')
        # Force the actual provider TU to see the shared declaration too. Its
        # private asm-labelled C name must have compatible mutable storage.
        command=[self.cc,"-O2","-Werror","-I",str(MEMORY),"-I",str(PLATFORM),
                 "-include",str(MEMORY/"p1_runtime_fdt_provider.h"),str(source),
                 str(MEMORY/"p1_memory_admission.c"),str(PLATFORM/"ppp_runtime_fdt.c"),"-o",str(self.root/"check")]
        r=subprocess.run(command,capture_output=True,text=True)
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertEqual(subprocess.run([str(self.root/"check")]).returncode,0)
        if shutil.which("ld.lld"):
            r=subprocess.run([*command[:1],"-flto=thin","-fuse-ld=lld",*command[1:]],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)
            self.assertEqual(subprocess.run([str(self.root/"check")]).returncode,0)
        missing=self.root/"missing.c"
        missing.write_text('#include "p1_runtime_fdt_provider.h"\nint main(void) { return p1_runtime_fdt_view()->bytes!=0; }\n')
        r=subprocess.run([self.cc,"-I",str(MEMORY),str(missing),"-o",str(self.root/"missing")],capture_output=True,text=True)
        self.assertNotEqual(r.returncode,0)
        self.assertIn("penang_p1_runtime_fdt",r.stderr)


if __name__=="__main__": unittest.main()

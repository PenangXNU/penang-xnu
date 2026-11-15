#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Host-only T73-U0 XNU ARM64 getpid source and carrier regression.

This does not execute XNU, deliver a valid Mach-O image or validate trust
admission. Source interpretation was checked against XNU 11417:
bsd/kern/syscalls.master; osfmk/arm64/proc_reg.h;
bsd/dev/arm/systemcalls.c; osfmk/arm64/sleh.c.
"""
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
FIX=ROOT/"tools"/"t73-bsd-userland"
ASM=FIX/"fixture_getpid.S"
LDS=FIX/"fixture_getpid.lds"
COOKIE=0x50454e414e475437
EXPECTED_POINTER=0x100004010
EXPECTED_MARKER=0x5437335f47455431


def tool(*names):
    return next((path for name in names if (path:=shutil.which(name))),None)


def run(argv):
    cp=subprocess.run([str(x) for x in argv],capture_output=True,text=True,
                      timeout=30,check=False)
    if cp.returncode:
        raise AssertionError(f"command failed rc={cp.returncode}: {argv[0]}\n{cp.stderr}")
    return cp.stdout


class GetpidFixture(unittest.TestCase):
    def test_source_invariants(self):
        s=ASM.read_text(encoding="utf-8")
        self.assertRegex(s,r"(?m)^\s*mov w16, #20\s")
        self.assertEqual(len(re.findall(r"(?m)^\s*svc #0x80\s*$",s)),1)
        self.assertIn("_fixture_before_syscall:",s)
        self.assertIn("_fixture_after_syscall:",s)
        self.assertIn("str x0, [x9, #8]",s)
        self.assertIn("mrs x14, nzcv",s)
        self.assertIn("tbnz x14, #29, _fixture_fail",s)
        self.assertIn("cbz x0, _fixture_fail",s)
        self.assertIn("tbnz x0, #63, _fixture_fail",s)
        self.assertIn("_fixture_pointer: .quad _fixture_cookie",s)
        self.assertIn("_fixture_syscall_raw_return: .quad 0",s)
        self.assertIn("_fixture_cookie: .quad 0x50454e414e475437",s)
        self.assertIn("ASSERT(SIZEOF(.data) == 40",LDS.read_text(encoding="utf-8"))
        self.assertNotIn("NO_MONITOR",s)
        self.assertNotIn("TRUSTCACHE_BYPASS",s)

    def test_aarch64_elf_carrier_layout_and_svc_encoding(self):
        clang=tool("clang-18","clang")
        linker=tool("ld.lld-18","ld.lld")
        objcopy=tool("llvm-objcopy-18","llvm-objcopy")
        objdump=tool("llvm-objdump-18","llvm-objdump")
        nm=tool("llvm-nm-18","llvm-nm")
        missing=[name for name,x in (("clang",clang),("lld",linker),
                 ("llvm-objcopy",objcopy),("llvm-objdump",objdump),
                 ("llvm-nm",nm)) if not x]
        if missing:
            self.skipTest("AArch64 LLVM host tools missing: "+", ".join(missing))
        with tempfile.TemporaryDirectory(prefix="t73-getpid-host-") as tmp:
            out=Path(tmp)
            obj=out/"fixture.o"
            elf=out/"fixture.elf"
            data=out/"fixture.data"
            run([clang,"--target=aarch64-none-elf","-mcpu=cortex-a76",
                 "-mstrict-align","-mgeneral-regs-only","-ffixed-x18",
                 "-fno-addrsig","-c",ASM,"-o",obj])
            run([linker,"-m","aarch64elf","--build-id=none","--no-undefined",
                 "--orphan-handling=error","-T",LDS,"-o",elf,obj])
            sym=run([nm,"-n",elf])
            symbols={}
            for line in sym.splitlines():
                items=line.split()
                if len(items)==3 and re.fullmatch(r"[0-9a-fA-F]+",items[0]):
                    symbols[items[2]]=int(items[0],16)
            self.assertEqual(symbols["_fixture_main"],0x100001000)
            self.assertEqual(symbols["_fixture_pointer"],0x100004000)
            self.assertEqual(symbols["_fixture_syscall_raw_return"],0x100004008)
            self.assertEqual(symbols["_fixture_cookie"],0x100004010)
            self.assertEqual(symbols["_fixture_result"],0x100004018)
            self.assertEqual(symbols["_fixture_error"],0x100004020)
            self.assertEqual(symbols["_fixture_after_syscall"],
                             symbols["_fixture_before_syscall"]+4)
            self.assertLess(symbols["_fixture_success"],0x100004000)
            dis=run([objdump,"-d",elf])
            self.assertIn("svc\t#0x80",dis)
            self.assertIn("mrs\tx14, NZCV",dis)
            self.assertRegex(dis,r"mov\tw16, #0x14\b")
            self.assertNotRegex(dis,r"\b[wx]18\b")
            run([objcopy,"--only-section=.data","-O","binary",elf,data])
            raw=data.read_bytes()
            self.assertEqual(len(raw),40)
            values=struct.unpack("<5Q",raw)
            self.assertEqual(values,(EXPECTED_POINTER,0,COOKIE,0,0))

    def test_marker_matches_documented_constant(self):
        s=ASM.read_text(encoding="utf-8")
        values=[]
        for fragment in ("0x5431","0x4745","0x335f","0x5437"):
            self.assertIn(fragment,s)
            values.append(int(fragment,16))
        got=sum(v<<(16*i) for i,v in enumerate(values))
        self.assertEqual(got,EXPECTED_MARKER)
        self.assertNotEqual(got,0x5437325f434f4e31)  # T72_CON1 baseline


if __name__=="__main__":
    unittest.main()

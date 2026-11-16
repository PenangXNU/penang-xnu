# SPDX-License-Identifier: Apache-2.0
"""MIG protocol-map and target ABI consumer negatives; no RPC execution."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/m12-penang-physical"))
spec = importlib.util.spec_from_file_location("p1_mig_headers", ROOT / "tools/m12-penang-physical/p1_mig_headers.py")
MOD = importlib.util.module_from_spec(spec)
spec.loader.exec_module(MOD)
DEFS = "subsystem vm_map 3800; routine vm_allocate(); skip; routine vm_deallocate();"
HEADER = '#include <mach/message.h>\n#define vm_map_MSG_COUNT 3\nstruct __Request__vm_allocate_t;\n{ "vm_allocate", 3800 }, { "vm_deallocate", 3802 }\n'


class MigHeaders(unittest.TestCase):
    def test_source_order_skip_and_header_map_are_independent(self):
        r = MOD.audit_header(DEFS, HEADER)
        self.assertEqual(r["count"], 3)
        self.assertEqual(r["routines"], {"vm_allocate": 3800, "vm_deallocate": 3802})

    def test_real_mig_keywords_are_case_insensitive(self):
        text = DEFS.replace("subsystem", "SubSystem").replace("routine", "SimpleRoutine").replace("skip", "SKIP")
        self.assertEqual(MOD.protocol(text), MOD.protocol(DEFS))

    def test_mutated_header_ids_counts_and_names_are_rejected(self):
        for bad in (HEADER.replace("3800", "3801"), HEADER.replace("COUNT 3", "COUNT 2"),
                    HEADER.replace('"vm_deallocate"', '"other"'), HEADER + '{"vm_allocate",3800}',
                    HEADER.replace("__Request__", "Missing"), "/*" + HEADER + "*/"):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                MOD.audit_header(DEFS, bad)

    def test_invalid_subsystems_and_duplicate_routines_are_rejected(self):
        for bad in ("", "subsystem empty 10; skip;", DEFS + "subsystem other 20;",
                    DEFS + " routine vm_allocate();", "subsystem big 1;" + "skip;" * 4097 + "routine x();"):
            with self.subTest(bad=bad[:60]), self.assertRaises(ValueError):
                MOD.protocol(bad)

    def test_kernel_user_mode_is_read_from_the_real_make_variable_shape(self):
        s = 'MIG_KUHDRS = \\\n\tclock_reply.h \\\n\tvm_map.h\nMIG_KUSRC = clock_reply_user.c vm_map_user.c\nOTHER = not_a_header\n'
        self.assertEqual(MOD.kernel_user_names(s), {"clock_reply", "vm_map"})
        with self.assertRaises(ValueError):
            MOD.kernel_user_names(s.replace("vm_map.h", "$(DYNAMIC)"))

    def test_server_header_modes_are_not_confused_with_kernel_server(self):
        make = ('MIG_KUHDRS = vm_map.h\nMIG_UUHDRS = clock.h vm_map.h\n'
                'MIG_USHDRS = clock_reply_server.h exc_server.h\n'
                'MIG_KSHDRS = clock_server.h exc_server.h\nMIG_KUSRC = vm_map_user.c\n')
        self.assertEqual(MOD.selection(make, "clock"), ("clock", "exported-user", False))
        self.assertEqual(MOD.selection(make, "vm_map"), ("vm_map", "kernel-user", True))
        self.assertEqual(MOD.selection(make, "exc_server"), ("exc", "exported-server", False))
        self.assertEqual(MOD.selection(make, "exc_server", True), ("exc", "kernel-server", False))
        with self.assertRaises(ValueError):
            MOD.selection(make, "clock_reply_server", True)
        for bad in ("clock_server", "unknown", "../clock", "clock_reply"):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                MOD.selection(make, bad)

    def test_extra_interfaces_require_their_real_directory_and_mode(self):
        self.assertEqual(MOD.selection("", "device_server", True, "device"),
                         ("device", "kernel-server", False))
        self.assertEqual(MOD.selection("", "UNDRequest", False, "UserNotification"),
                         ("UNDRequest", "kernel-user", True))
        for name, mode, directory in (("device_server", False, "device"),
                                      ("UNDRequest", True, "UserNotification"),
                                      ("clock", False, "device"), ("UNDRequest", False, "../other")):
            with self.subTest(name=name), self.assertRaises(ValueError):
                MOD.selection("", name, mode, directory)

    def test_actual_cpp_profile_keeps_private_slots_and_explicit_server_mode(self):
        cc = shutil.which("clang-18") or shutil.which("clang")
        if not cc:
            self.skipTest("Clang required")
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            source = root / "profile.defs"
            source.write_text("""subsystem profile 200;
#ifdef PRIVATE
routine private_api();
#else
skip;
#endif
#if CONFIG_ROSETTA
routine rosetta_api();
#else
skip;
#endif
#if KERNEL_USER
routine ku_api();
#endif
#if KERNEL_SERVER
routine ks_api();
#endif
""")
            def preprocess(mode, flags, erase_private=False):
                command = MOD.preprocessor_command(cc, root, flags, source, mode)
                if erase_private:
                    command.insert(-1, "-UPRIVATE")
                run = subprocess.run(command, capture_output=True, text=True, timeout=20)
                self.assertEqual(run.returncode, 0, run.stderr)
                return MOD.protocol(run.stdout)
            expected = {"private_api": 200, "rosetta_api": 201, "ku_api": 202}
            self.assertEqual(preprocess("kernel-user", ["-DCONFIG_ROSETTA=1"])["routines"], expected)
            self.assertNotEqual(preprocess("kernel-user", ["-DCONFIG_ROSETTA=1"], True)["routines"], expected)
            self.assertNotEqual(preprocess("kernel-user", ["-DCONFIG_ROSETTA=0"])["routines"], expected)
            self.assertEqual(preprocess("kernel-server", ["-DCONFIG_ROSETTA=1"])["routines"],
                             {"private_api": 200, "rosetta_api": 201, "ks_api": 202})
            self.assertEqual(preprocess("exported-server", ["-DCONFIG_ROSETTA=0"])["routines"],
                             {"private_api": 200})

    def test_driver_injection_environment_is_not_inherited(self):
        with mock.patch.dict(os.environ, {"CCC_OVERRIDE_OPTIONS": "+-DFORGED=1", "CPATH": "bad", "SDKROOT": "bad"}):
            env = MOD.environment()
        for key in ("CCC_OVERRIDE_OPTIONS", "CPATH", "SDKROOT"):
            self.assertNotIn(key, env)

    def test_implementations_follow_kernel_source_lists_not_exported_header_presence(self):
        make = ("MIG_KUSRC = \\\n\tvm_map_user.c\n"
                "MIG_KSSRC = \\\n\tclock_server.c\n")
        self.assertEqual(MOD.implementation_name(make, "vm_map", "kernel-user", "mach"), "vm_map_user.c")
        self.assertEqual(MOD.implementation_name(make, "clock_server", "kernel-server", "mach"), "clock_server.c")
        self.assertIsNone(MOD.implementation_name(make, "host_security_server", "kernel-server", "mach"))
        self.assertIsNone(MOD.implementation_name(make, "clock", "exported-user", "mach"))
        self.assertIsNone(MOD.implementation_name(make, "exc_server", "exported-server", "mach"))
        self.assertEqual(MOD.implementation_name("", "UNDRequest", "kernel-user", "UserNotification"), "UNDRequest.c")
        self.assertEqual(MOD.implementation_name("", "UNDReplyServer", "kernel-server", "UserNotification"), "UNDReplyServer.c")
        self.assertEqual(MOD.implementation_name("", "device_server", "kernel-server", "device"), "device_server.c")
        for subsystem in ("gssd", "kextd", "lockd"):
            self.assertEqual(MOD.selection("", subsystem + "_mach", subsystem=subsystem), (subsystem + "_mach", "kernel-user", True))
            self.assertEqual(MOD.implementation_name("", subsystem+"_mach", "kernel-user", subsystem), subsystem+"_mach.c")
            with self.assertRaises(ValueError): MOD.selection("", subsystem+"_mach", True, subsystem)
        for name, mode, subsystem in (("other", "kernel-server", "device"), ("UNDRequest", "kernel-server", "UserNotification"), ("x", "kernel-user", "bsd")):
            with self.assertRaises(ValueError): MOD.implementation_name(make, name, mode, subsystem)
        with self.assertRaises(ValueError): MOD.implementation_name("MIG_KUSRC = $(INJECTED)\n", "x", "kernel-user", "mach")

    def test_unadmitted_generator_cannot_create_output(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            gen = root / "untrusted"
            gen.write_text("not a generator")
            out = root / "out"
            for pin in ("bad", "0" * 64):
                with self.subTest(pin=pin), self.assertRaises(ValueError):
                    MOD.generate(root, gen, pin, ["clock"], out)
                self.assertFalse(out.exists())

    def test_real_c_cpp_consumers_detect_prototype_and_layout_mutations(self):
        cc = shutil.which("clang-18") or shutil.which("clang")
        if not cc:
            self.skipTest("Clang required")
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "mach").mkdir()
            # Deliberately synthetic ABI test headers, not substitutes admitted
            # by generate(), which requires original Git and source pins.
            common = '''#pragma once
typedef int kern_return_t;
typedef void *clock_serv_t;
typedef void *vm_map_t;
typedef unsigned long long vm_address_t, vm_size_t;
typedef struct { unsigned sec, nsec; } mach_timespec_t;
typedef struct { unsigned words[8]; } mach_msg_header_t;
typedef struct { unsigned char words[8]; } NDR_record_t;
#pragma pack(push,4)
typedef struct { mach_msg_header_t Head; } __Request__clock_get_time_t;
typedef struct { mach_msg_header_t Head; NDR_record_t NDR; kern_return_t RetCode; mach_timespec_t cur_time; } __Reply__clock_get_time_t;
#pragma pack(pop)
#define clock_MSG_COUNT 3
#define vm_map_MSG_COUNT 33
'''
            (root / "mach/clock.h").write_text(common + 'kern_return_t clock_get_time(clock_serv_t, mach_timespec_t *);\n')
            prototype = 'kern_return_t vm_allocate(vm_map_t, vm_address_t *, vm_size_t, int);\n'
            (root / "mach/vm_map.h").write_text(prototype)
            source = root / "probe.c"
            source.write_text(MOD.CONSUMER)
            for language in ("c", "c++"):
                def compile_it():
                    return subprocess.run([cc, "--target=arm64-apple-darwin", "-x", language,
                                           "-I", str(root), "-fsyntax-only", str(source)],
                                          capture_output=True, text=True, timeout=20)
                self.assertEqual(compile_it().returncode, 0)
                (root / "mach/vm_map.h").write_text(prototype.replace("vm_size_t,", "int,"))
                self.assertNotEqual(compile_it().returncode, 0)
                (root / "mach/vm_map.h").write_text(prototype)
                for mutated in (common.replace('unsigned char words[8]', 'unsigned char words[4]'),
                                common.replace('kern_return_t RetCode; mach_timespec_t cur_time;',
                                               'mach_timespec_t cur_time; kern_return_t RetCode;')):
                    (root / "mach/clock.h").write_text(mutated + 'kern_return_t clock_get_time(clock_serv_t, mach_timespec_t *);\n')
                    self.assertNotEqual(compile_it().returncode, 0)
                (root / "mach/clock.h").write_text(common + 'kern_return_t clock_get_time(clock_serv_t, mach_timespec_t *);\n')

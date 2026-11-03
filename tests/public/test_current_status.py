# SPDX-License-Identifier: Apache-2.0
"""v0.1.0 status derivation: pinned P0.10/P0.11/OBS-D1 references; no research runtime is imported or executed."""
from pathlib import Path
import importlib.util
import shutil
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("current_status", ROOT / "scripts/current_status.py")
status = importlib.util.module_from_spec(spec)
spec.loader.exec_module(status)


class CurrentStatusTests(unittest.TestCase):
    def setUp(self):
        self.data = status.read(ROOT / status.SOURCE)

    def fixture(self):
        base = ROOT / ".build/v0.1/status-tests"
        base.mkdir(parents=True, exist_ok=True)
        temp = tempfile.TemporaryDirectory(dir=base)
        self.addCleanup(temp.cleanup)
        root = Path(temp.name)
        for n in [status.SOURCE, status.SCHEMA, status.LOCK, "scripts/current_status.py",
                  "README.md", "STATUS.md", "manifests/current-status.json"]:
            p = root / n
            p.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / n, p)
        return root

    def rejected(self):
        with self.assertRaises(ValueError):
            status.validate_projection(self.data)

    def test_research_scope_and_hardware_limits(self):
        d, pins = status.load(ROOT)
        result = status.derive(d, pins)
        self.assertEqual(result["research_b3"], "B3_RESEARCH_PROVIDER_RUNTIME_VERIFIED")
        self.assertEqual(result["research_b4"], "B4_RESEARCH_PLATFORM_EXPERT_RUNTIME_VERIFIED")
        self.assertEqual(result["apple_ipados_security"], "NOT_ESTABLISHED")
        self.assertEqual(result["research_gic"]["scope"], "QEMU_VIRT_TCG_SINGLE_CPU")
        self.assertEqual(result["next_blocker"]["id"], "B6_IOPMGR")
        self.assertEqual(result["next_blocker"]["independent_blocker"], "B7_PTHREAD_PROVIDER")
        self.assertEqual(result["device_observability"]["xnu_on_device"], "NOT_RUN")
        self.assertEqual(result["hardware"], status.HARDWARE)

    def test_generated_documents_match(self):
        self.assertEqual(status.check(ROOT)["status"], "PASS")

    def test_source_tamper_without_lock_is_rejected(self):
        root = self.fixture()
        p = root / status.SOURCE
        p.write_bytes(p.read_bytes() + b"\n")
        with self.assertRaises(ValueError):
            status.load(root)

    def test_generated_drift_is_rejected(self):
        root = self.fixture()
        p = root / "STATUS.md"
        p.write_text(p.read_text() + "manual edit\n")
        with self.assertRaises(ValueError):
            status.check(root)

    def test_moved_research_pin_is_rejected(self):
        self.data["commits"]["p11"] = "b" * 40
        self.rejected()

    def test_incorrect_source_hash_is_rejected(self):
        self.data["sources"]["reports/P0.11-GATES.json"]["sha256"] = "0" * 64
        self.rejected()

    def test_included_payload_is_rejected(self):
        self.data["sources"]["reports/penang/D1-PENANG-FASTBOOT-BOOT-20261102.md"]["payload_included"] = True
        self.rejected()

    def test_apple_equivalence_promotion_is_rejected(self):
        self.data["p10"]["apple_ipados_security_compatible"] = "APPLE_IPADOS_SECURITY_COMPATIBLE"
        self.rejected()

    def test_apple_platform_identity_claim_is_rejected(self):
        self.data["p11"]["apple_platform_identity_claimed"] = True
        self.rejected()

    def test_userland_promotion_is_rejected(self):
        self.data["p11"]["userland_executed"] = True
        self.rejected()

    def test_device_or_gpu_promotion_is_rejected(self):
        self.data["hardware"]["physical_GPU_progress"] = True
        self.rejected()

    def test_android_ram_boot_is_not_xnu_on_device(self):
        self.data["device_observability"]["xnu_on_device"] = "PASS"
        self.rejected()

    def test_unknown_granule_cannot_be_promoted(self):
        self.data["device_observability"]["tgran16"] = "SUPPORTED"
        self.rejected()

    def test_missing_p10_negative_check_is_rejected(self):
        self.data["p10"]["negative"]["checks"].pop("unauthorized_module_refused")
        self.rejected()

    def test_missing_p11_negative_control_is_rejected(self):
        self.data["p11"]["identity_refused"]["checks"].pop("probe_refused_with_defined_error")
        self.rejected()

    def test_failed_p11_check_is_rejected(self):
        self.data["p11"]["positive"]["checks"]["iopanicplatform_not_started"] = False
        self.rejected()

    def test_p12_scope_cannot_claim_smp(self):
        self.data["p12"]["smp"] = "QUALIFIED"
        self.rejected()

    def test_p12_sgi_count_cannot_be_promoted(self):
        self.data["p12"]["sgi1"]["handler_calls"] = 99
        self.rejected()

    def test_p12_missing_evidence_rejected(self):
        self.data["sources"].pop("reports/P0.12-GATES.json")
        self.rejected()

    def test_p12_bsd_cannot_claim_userland(self):
        self.data["p12"]["userland"] = "RUNNING"
        self.rejected()

    def test_hidden_next_blocker_is_rejected(self):
        self.data["next_blocker"]["id"] = "NONE"
        self.rejected()

    def test_interface_expansion_is_rejected(self):
        self.data["p10"]["image4_research_interface_version"] = 20
        self.rejected()


if __name__ == "__main__":
    unittest.main()

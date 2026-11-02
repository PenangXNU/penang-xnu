"""Public W2/W3 CLI tests: synthetic registers are never runtime evidence."""
from pathlib import Path
import copy
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "tools/penang-validate/validate.py"
CORE = ROOT / ".build/w2w3/cargo/debug/penang-validate"

# Independently hand-derived: TGran16=1, PARange=2 (40bit).
OBSERVATION = {
    "schema_version": "1.0",
    "platform": "qemu-virt",
    "subject": "VIRTUAL_CPU",
    "environment": "HOST_SYNTHETIC",
    "snapshot": {
        "valid": 1, "current_el": 4, "midr_el1": 0x410FD0B0,
        "id_aa64mmfr0_el1": 0x100002, "sctlr_el1": 0,
        "observed_tcr_el1": 0, "cntfrq_el0": 62500000
    },
    # 16KiB TG0/TG1, 40bit IPS, 47bit lower/upper VA; normal WB/outer-shareable.
    "desired_tcr_el1": 0x226511A511,
    "memory": [
        {"base": 0x40000000, "size": 0x20000000, "kind": 1},
        {"base": 0x44000000, "size": 0x2000000, "kind": 2},
        {"base": 0x46000000, "size": 0x10000, "kind": 3}
    ]
}


class PlatformTests(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / ".build/w2w3/test-tmp"
        scratch.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=scratch)
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        self.observation = copy.deepcopy(OBSERVATION)

    def run_cli(self, platform="qemu-virt", observation=True, root=None, extra=()):
        args = [sys.executable, "-B", str(CLI), "--root", str(root or ROOT),
                "--platform", platform, "--core", str(CORE), *extra]
        if observation:
            path = self.work / "observation.json"
            path.write_text(json.dumps(self.observation), encoding="utf-8")
            args += ["--observation", str(path), "--contract-only"]
        result = subprocess.run(args, text=True, capture_output=True, timeout=30)
        return result

    def result(self, **kwargs):
        result = self.run_cli(**kwargs)
        self.assertTrue(result.stdout.strip().startswith("{"), result.stdout + result.stderr)
        return result, json.loads(result.stdout)

    def failure(self, code, **kwargs):
        result, data = self.result(**kwargs)
        self.assertEqual(result.returncode, 1, data)
        self.assertEqual(data["status"], "FAIL")
        self.assertIn(code, {c["code"] for c in data["checks"]}, data)

    def fixture_root(self):
        root = self.work / "repo"
        for name in ("contracts/platform", "platforms"):
            shutil.copytree(ROOT / name, root / name)
        registry = json.loads((ROOT / "contracts/platform/evidence.json").read_text())
        for entry in registry["sources"].values():
            path = ROOT / entry["path"]
            target = root / entry["path"]
            if not target.exists():
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path, target)
        return root

    def change_profile(self, root, platform, edit):
        path = root / "platforms" / platform / "profile.json"
        value = json.loads(path.read_text())
        edit(value)
        path.write_text(json.dumps(value), encoding="utf-8")

    def test_valid_qemu_snapshot_checks_translation_and_reservations(self):
        result, data = self.result()
        self.assertEqual(result.returncode, 0, data)
        self.assertEqual(data["status"], "PASS")
        self.assertEqual(data["scope"], "HOST_CONTRACT")
        self.assertEqual(data["runtime_status"], "NOT_RUN")
        self.assertFalse(data["runtime_enabled"])
        self.assertEqual(data["translation"]["status"], "PASS")
        self.assertEqual(data["memory"]["status"], "PASS")

    def test_qemu_without_current_registers_stays_blocked(self):
        result, data = self.result(observation=False)
        self.assertEqual(result.returncode, 2, data)
        self.assertEqual(data["status"], "BLOCKED")
        self.assertEqual(data["translation"]["status"], "NOT_RUN")

    def test_holi_inventory_does_not_enable_xnu(self):
        result, data = self.result(platform="qualcomm-holi", observation=False)
        self.assertEqual(result.returncode, 2, data)
        self.assertEqual(data["status"], "BLOCKED")
        self.assertFalse(data["runtime_enabled"])
        self.assertEqual(data["capabilities"]["granule_16k"]["state"], "UNKNOWN")
        self.assertEqual(data["legacy_bsp_status"], "PASS_HOST_METADATA")

    def test_trinket_unknown_identity_is_preserved(self):
        result, data = self.result(platform="qualcomm-trinket", observation=False)
        self.assertEqual(result.returncode, 2, data)
        self.assertEqual(data["identity_status"], "UNKNOWN")
        self.assertFalse(data["runtime_enabled"])

    def test_synthetic_snapshot_cannot_be_called_qemu_observation(self):
        self.observation["environment"] = "QEMU_OBSERVATION"
        self.failure("UNADMITTED_OBSERVATION")

    def test_granule_unsupported(self):
        self.observation["snapshot"]["id_aa64mmfr0_el1"] = 2
        self.failure("GRANULE_UNSUPPORTED")

    def test_reserved_feature_encoding_blocks(self):
        self.observation["snapshot"]["id_aa64mmfr0_el1"] = 0x300002
        result, data = self.result()
        self.assertEqual(result.returncode, 2, data)
        self.assertEqual(data["status"], "BLOCKED")

    def test_tg0_incompatible(self):
        self.observation["desired_tcr_el1"] &= ~(3 << 14)
        self.failure("TCR_GRANULE")

    def test_tg1_incompatible(self):
        self.observation["desired_tcr_el1"] &= ~(3 << 30)
        self.failure("TCR_GRANULE")

    def test_insufficient_physical_address_range(self):
        self.observation["snapshot"]["id_aa64mmfr0_el1"] = 0x100001
        self.failure("PA_RANGE")

    def test_va_configuration_mismatch(self):
        self.observation["desired_tcr_el1"] ^= 1
        self.failure("TCR_VA")

    def test_lpa2_not_silently_enabled(self):
        self.observation["desired_tcr_el1"] |= 1 << 59
        self.failure("TCR_MODE")

    def test_unqualified_translation_flag_is_rejected(self):
        self.observation["desired_tcr_el1"] |= 1 << 39
        self.failure("TCR_MODE")

    def test_wrong_exception_level(self):
        self.observation["snapshot"]["current_el"] = 8
        self.failure("EXCEPTION_LEVEL")

    def test_mmu_already_enabled_rejected_for_pre_mmu_contract(self):
        self.observation["snapshot"]["sctlr_el1"] = 1
        self.failure("MMU_STATE")

    def test_cpu_model_is_not_falsified(self):
        self.observation["snapshot"]["midr_el1"] = 0x510F8050
        self.failure("CPU_IDENTITY")

    def test_virtual_snapshot_does_not_qualify_physical_platform(self):
        self.failure("PLATFORM_SUBJECT", platform="qualcomm-holi")

    def test_ram_overflow(self):
        self.observation["memory"][0]["base"] = (1 << 64) - 0x4000
        self.failure("MEMORY_OVERFLOW")

    def test_reserved_region_outside_ram(self):
        self.observation["memory"][2]["base"] = 0x70000000
        self.failure("MEMORY_CONTAINMENT")

    def test_reservations_overlap(self):
        self.observation["memory"][2]["base"] = 0x44004000
        self.failure("MEMORY_OVERLAP")

    def test_reservation_is_page_aligned(self):
        self.observation["memory"][2]["base"] += 1
        self.failure("MEMORY_ALIGNMENT")

    def test_kernel_reservation_is_required(self):
        self.observation["memory"] = [r for r in self.observation["memory"] if r["kind"] != 2]
        self.failure("MEMORY_REQUIRED")

    def test_boot_data_reservation_is_required(self):
        self.observation["memory"] = [r for r in self.observation["memory"] if r["kind"] != 3]
        self.failure("MEMORY_REQUIRED")

    def test_empty_map_is_not_qualified(self):
        self.observation["memory"] = []
        self.failure("MEMORY_REQUIRED")

    def test_missing_required_capability(self):
        root = self.fixture_root()
        self.change_profile(root, "qemu-virt", lambda d: d["capabilities"].pop("arch_timer"))
        self.failure("REQUIRED_CAPABILITY", root=root)

    def test_platform_backend_mismatch(self):
        root = self.fixture_root()
        self.change_profile(root, "qemu-virt", lambda d: d.update(backend="qualcomm-holi-v1"))
        self.failure("BACKEND_MISMATCH", root=root)

    def test_identity_cannot_be_promoted_by_json(self):
        root = self.fixture_root()
        self.change_profile(root, "qualcomm-trinket", lambda d: d.update(identity_status="VERIFIED"))
        self.failure("IDENTITY_PROVENANCE", root=root, platform="qualcomm-trinket", observation=False)

    def test_descriptor_cannot_claim_current_xnu_operational(self):
        root = self.fixture_root()
        self.change_profile(root, "qualcomm-holi",
                            lambda d: d["resources"]["interrupts"].update(xnu_operational="SUPPORTED"))
        self.failure("CAPABILITY_CONTRADICTION", root=root, platform="qualcomm-holi", observation=False)

    def test_offline_gic_addresses_match_audited_inventory(self):
        root = self.fixture_root()
        self.change_profile(root, "qualcomm-holi",
                            lambda d: d["resources"]["interrupts"]["offline_descriptor"]["distributor"].update(base="0xDEADBEEF"))
        self.failure("RESOURCE_PROVENANCE", root=root, platform="qualcomm-holi", observation=False)

    def test_absent_evidence(self):
        root = self.fixture_root()
        self.change_profile(root, "qemu-virt",
                            lambda d: d["capabilities"]["arch_timer"].update(evidence_id="MISSING"))
        self.failure("EVIDENCE_MISSING", root=root)

    def test_json_cannot_promote_its_own_evidence_admission(self):
        root = self.fixture_root()
        self.change_profile(root, "qualcomm-holi",
                            lambda d: d["capabilities"]["granule_16k"].update(state="SUPPORTED"))
        path = root / "contracts/platform/evidence.json"
        data = json.loads(path.read_text())
        data["sources"]["HOLI_CPU"]["claims"]["granule_16k"] = ["SUPPORTED"]
        path.write_text(json.dumps(data))
        self.failure("EVIDENCE_INTEGRITY", root=root, platform="qualcomm-holi", observation=False)

    def test_changed_evidence_bytes(self):
        root = self.fixture_root()
        path = root / "evidence/platform-spec.json"
        path.write_bytes(path.read_bytes() + b"\n")
        self.failure("EVIDENCE_INTEGRITY", root=root)

    def test_unsupported_capability_cannot_be_enabled(self):
        root = self.fixture_root()
        self.change_profile(root, "qemu-virt",
                            lambda d: d["capabilities"]["granule_16k"].update(state="UNSUPPORTED", enabled=True))
        self.failure("CAPABILITY_CONTRADICTION", root=root)

    def test_static_specification_cannot_become_device_observation(self):
        root = self.fixture_root()
        self.change_profile(root, "qemu-virt",
                            lambda d: d["capabilities"]["arch_timer"].update(origin="DEVICE_OBSERVATION"))
        self.failure("ORIGIN_MISMATCH", root=root)

    def test_w1_target_contract_is_reused(self):
        root = self.fixture_root()
        path = root / "contracts/platform/foundation/manifests/target-contract.json"
        d = json.loads(path.read_text())
        d["os_target"]["build"] = "22H999"
        path.write_text(json.dumps(d))
        self.failure("FOUNDATION", root=root)

    def test_json_is_reproducible(self):
        a = self.run_cli()
        b = self.run_cli()
        self.assertEqual(a.returncode, 0, a.stdout + a.stderr)
        self.assertEqual(a.stdout, b.stdout)


if __name__ == "__main__":
    unittest.main()

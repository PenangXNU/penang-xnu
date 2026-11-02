"""Fixed P0.10 status derivation; no research runtime is imported or executed."""
from pathlib import Path
import copy
import importlib.util
import json
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
        base = ROOT / ".build/rc2/status-tests"
        base.mkdir(parents=True, exist_ok=True)
        temp = tempfile.TemporaryDirectory(dir=base)
        self.addCleanup(temp.cleanup)
        root = Path(temp.name)
        names = [status.SOURCE,status.SCHEMA,status.LOCK,"scripts/current_status.py",
                 "README.md","STATUS.md","manifests/current-status.json"]
        for n in names:
            p = root / n
            p.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(ROOT / n, p)
        return root

    def test_fixed_research_scope_and_hardware_limits(self):
        d, pins = status.load(ROOT)
        result = status.derive(d,pins)
        self.assertEqual(result["research_b3"],"B3_RESEARCH_PROVIDER_RUNTIME_VERIFIED")
        self.assertEqual(result["apple_ipados_security"],"NOT_ESTABLISHED")
        self.assertFalse(result["next_blocker"]["valid_platform_expert_initialized"])
        self.assertEqual(result["hardware"],status.HARDWARE)

    def test_generated_documents_match(self):
        self.assertEqual(status.check(ROOT)["status"],"PASS")

    def test_source_tamper_without_lock_is_rejected(self):
        root = self.fixture()
        p = root / status.SOURCE
        p.write_bytes(p.read_bytes()+b"\n")
        with self.assertRaises(ValueError):
            status.load(root)

    def test_future_b4_commit_cannot_move_this_rc(self):
        self.data["source_commit"] = "b"*40
        with self.assertRaises(ValueError):
            status.validate_projection(self.data)

    def test_apple_equivalence_promotion_is_rejected(self):
        self.data["p10"]["apple_ipados_security_compatible"]="APPLE_IPADOS_SECURITY_COMPATIBLE"
        with self.assertRaises(ValueError):
            status.validate_projection(self.data)

    def test_device_or_gpu_promotion_is_rejected(self):
        self.data["hardware"]["physical_GPU_progress"]=True
        with self.assertRaises(ValueError):
            status.validate_projection(self.data)

    def test_missing_negative_authentication_check_is_rejected(self):
        self.data["negative"]["checks"].pop("unauthorized_module_refused")
        with self.assertRaises(ValueError):
            status.validate_projection(self.data)

    def test_generated_drift_is_rejected(self):
        root = self.fixture()
        p = root/"STATUS.md"
        p.write_text(p.read_text()+"manual edit\n")
        with self.assertRaises(ValueError):
            status.check(root)

    def test_interface_expansion_is_rejected(self):
        self.data["p10"]["image4_research_interface_version"]=20
        with self.assertRaises(ValueError):
            status.validate_projection(self.data)

    def test_incorrect_source_hash_is_rejected(self):
        self.data["sources"]["reports/P0.10-GATES.json"]["sha256"]="0"*64
        with self.assertRaises(ValueError):
            status.validate_projection(self.data)

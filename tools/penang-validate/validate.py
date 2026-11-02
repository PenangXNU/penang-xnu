#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""penang-validate v0.1: selected W1 core + schemas + native Rust/C platform checks."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")
try:
    from jsonschema import Draft202012Validator
except ImportError:
    print(json.dumps({"status": "NOT_RUN", "runtime_status": "NOT_RUN", "runtime_enabled": False,
          "checks": [{"status": "NOT_RUN", "code": "DEPENDENCY", "message": "jsonschema unavailable; no auto-install"}]}))
    sys.exit(3)

W1_TOOL_SHA256 = "14bc8b477ea2989fb7684d138b73e75382b57041b0e0f69b8b001ff98297f9e9"


def no_duplicates(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON key")
        result[key] = value
    return result


def read(path):
    return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=no_duplicates)


def check(status, code, message):
    return {"status": status, "code": code, "message": str(message)[:600]}


def safe(root, relative):
    relative = Path(relative)
    if relative.is_absolute() or ".." in relative.parts:
        raise ValueError("reference must remain relative to repository")
    path = (root / relative).resolve()
    if not path.is_relative_to(root.resolve()):
        raise ValueError("reference escapes repository")
    return path


def schema_errors(data, path):
    schema = read(path)
    def external(value):
        if isinstance(value, dict):
            return any(k == "$ref" and not str(v).startswith("#") or external(v) for k, v in value.items())
        return isinstance(value, list) and any(external(v) for v in value)
    if external(schema):
        return [check("FAIL", "SCHEMA_REFERENCE", "external schema references forbidden offline")]
    Draft202012Validator.check_schema(schema)
    errors = []
    for e in Draft202012Validator(schema).iter_errors(data):
        code = "REQUIRED_CAPABILITY" if e.validator == "required" and list(e.path) == ["capabilities"] else "SCHEMA"
        errors.append(check("FAIL", code, e.message))
    return errors


def foundation(root):
    module_path = Path(__file__).with_name("foundation_v1.py")
    if hashlib.sha256(module_path.read_bytes()).hexdigest() != W1_TOOL_SHA256:
        return [check("FAIL", "FOUNDATION", "selected W1 validator bytes changed")]
    spec = importlib.util.spec_from_file_location("foundation_v1", module_path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    bundle = root / "contracts/platform/foundation"
    audit = module.Audit(bundle)
    data = [audit.json(n) for n in module.INPUTS]
    if all(isinstance(d, dict) for d in data):
        target, evidence, rules, register = data
        audit.authority(target, evidence)
        audit.schemas(target, evidence)
        if not audit.errors:
            audit.lock(target, register)
            audit.contract(target, register)
            audit.provenance(target, evidence, rules, register)
        if not audit.errors:
            actual = audit.json("manifests/project-status.json")
            if actual != module.derive(target, evidence, rules):
                audit.error("STATUS_DERIVATION", "project-status", "not derived from selected W1 evidence")
    return [check("FAIL", "FOUNDATION", e["code"] + ": " + e["message"]) for e in audit.errors]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--platform", choices=("qemu-virt", "qualcomm-holi", "qualcomm-trinket"), default="qemu-virt")
    parser.add_argument("--profile", help="repository-relative profile; defaults to selected platform")
    parser.add_argument("--observation", type=Path, help="explicit synthetic snapshot, never runtime proof")
    parser.add_argument("--contract-only", action="store_true")
    parser.add_argument("--core", type=Path, help="already-built native Rust/C executable")
    args = parser.parse_args()
    root = args.root.resolve()
    core = args.core or root / ".build/w2w3/cargo/debug/penang-validate"
    profile = args.profile or "platforms/" + args.platform + "/profile.json"
    checks = []
    try:
        checks.extend(foundation(root))
        checks.extend(schema_errors(read(safe(root, profile)), root / "contracts/platform/platform.schema.json"))
        if args.observation:
            checks.extend(schema_errors(read(args.observation), root / "contracts/platform/observation.schema.json"))
    except (OSError, ValueError, TypeError, KeyError) as e:
        checks.append(check("FAIL", "INPUT", e))
    if checks:
        result = {"schema_version": "0.1", "status": "FAIL", "scope": "INPUT_VALIDATION",
                  "runtime_status": "NOT_RUN", "runtime_enabled": False, "checks": checks}
        exit_code = 1
    elif not core.is_file():
        result = {"schema_version": "0.1", "status": "NOT_RUN", "runtime_status": "NOT_RUN",
                  "runtime_enabled": False, "checks": [check("NOT_RUN", "CORE_NOT_BUILT", "build the native core offline first")]}
        exit_code = 3
    else:
        command = [str(core), "--root", str(root), "--platform", args.platform, "--profile", profile]
        if args.observation:
            command += ["--observation", str(args.observation)]
        if args.contract_only:
            command += ["--contract-only"]
        try:
            native = subprocess.run(command, text=True, capture_output=True, timeout=30, check=False)
            result = json.loads(native.stdout)
            if native.returncode not in (0, 1, 2, 3):
                raise ValueError("unexpected native process exit")
            result["checks"].append(check("PASS", "FOUNDATION", "selected W1 schemas/provenance/ADRs/status reused unchanged"))
            result["checks"].append(check("PASS", "SCHEMA", "platform and observation schemas"))
            result["foundation_scope"] = "SELECTED_W1_CORE; historical documents/private payloads not imported"
            exit_code = native.returncode
        except (OSError, ValueError, subprocess.TimeoutExpired) as e:
            result = {"status": "NOT_RUN", "runtime_status": "NOT_RUN", "runtime_enabled": False,
                      "checks": [check("NOT_RUN", "NATIVE_EXECUTION", e)]}
            exit_code = 3
    print(json.dumps(result, ensure_ascii=False, sort_keys=True, indent=2))
    return exit_code


if __name__ == "__main__":
    sys.exit(main())

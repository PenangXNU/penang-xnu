#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Derive RC2 current status above immutable W1/W2/W3/W4 snapshots; offline."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import sys
from jsonschema import Draft202012Validator

ROOT = Path(__file__).resolve().parents[1]
PIN = "afa7ef6acfb89bf5ae2f3d0c67e7a775096f39b0"
SOURCE = "evidence/current-status-sources.json"
SCHEMA = "schemas/current-status.schema.json"
LOCK = "evidence/current-status.lock.json"
BEGIN = "<!-- CURRENT_STATUS:BEGIN -->"
END = "<!-- CURRENT_STATUS:END -->"
EXPECTED = {
 "reports/P0.10-HANDOFF.md": "af5002117d4142358d20bf7f9b64874545a87068b7611f7f8dc1acabf9e0ac57",
 "reports/P0.10-GATES.json": "c0518917aef62ad52af60f7dc86b25dce58fe5f9617c6d6602f3a2dc720fb164",
 "reports/P0.10-TEST-RESULTS.json": "52d8f909881426b72c22925d30ac5f7f2b78ea73319e4c24eedc31bae26c5d3c",
 "reports/p10/qemu-check-accept.json": "6a597f203f2f3be02b55efb2c613b8442e3243cbac73e24df491bc4f5dee6acb",
 "reports/p10/qemu-check-reject.json": "9660abbb7501ad5067faba2df0409e3d2243b4240b0ad6e568f8a79e920da7ae",
}
HARDWARE = {"Moto_G53_XNU_boot":"NOT_DEMONSTRATED", "D1_D4":"NOT_DEMONSTRATED",
            "M9":"HOST_ONLY", "Sofiar":"P1_UNKNOWN", "physical_GPU_progress":False,
            "SpringBoard_demonstrated":False}
POSITIVE = {"state_observed", "trust_cache_placed_by_loader", "p09_crypto_lines_unchanged",
 "prng_registered", "providers_registered_once", "order_crypto_then_research_then_trustcache",
 "b3_panic_absent", "research_tc_loader_reached", "static_trust_cache_loaded",
 "no_trust_cache_failure", "next_stop_recorded"}
NEGATIVE = {"state_observed", "trust_cache_placed_by_loader", "p09_crypto_lines_unchanged",
 "prng_registered", "providers_registered_once", "order_crypto_then_research_then_trustcache",
 "b3_panic_absent", "research_tc_loader_reached", "unauthorized_module_refused",
 "fail_closed_guard", "nothing_loaded"}

def require(condition, message):
    if not condition:
        raise ValueError(message)

def sha(data):
    return hashlib.sha256(data).hexdigest()

def pairs(rows):
    d = {}
    for k, v in rows:
        require(k not in d, "duplicate JSON key")
        d[k] = v
    return d

def read(path):
    return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=pairs)

def validate_projection(d):
    require(d["release"] == "W4-RC2" and d["source_commit"] == PIN, "RC2 source commit is frozen")
    require(d["runtime_reproduced_by_RC2"] is False, "documentary reference is not a new QEMU test")
    require(d["evidence_kind"] == "DOCUMENTARY_REFERENCE_TO_QEMU_RESULTS", "evidence scope changed")
    require(set(d["sources"]) == set(EXPECTED), "exact five pinned documents required")
    for n, h in EXPECTED.items():
        r = d["sources"][n]
        require(r["commit"] == PIN and r["sha256"] == h and r["payload_included"] is False, "source identity changed")
    p = d["p10"]
    require(p["research_state"] == "B3_RESEARCH_PROVIDER_RUNTIME_VERIFIED", "research status changed")
    require(p["apple_ipados_security_compatible"] == "NOT_ESTABLISHED", "research is not Apple compatibility")
    require(p["trust_cache"] == "PNRA-R1_AUTHENTICATED" and p["trust_root"] == "PROJECT_RESEARCH_ROOT_SEPARATE_FROM_APPLE", "trust scope changed")
    require(p["image4_research_interface_version"] == 7 and p["operations"] == "RESTRICTED_DEFINED_REFUSALS", "interface scope changed")
    require(all(p[k] is False for k in ["apple_entitlements","sep_support","complete_userland_support"]), "unsupported security claim")
    for name, fields in [("positive", POSITIVE), ("negative", NEGATIVE)]:
        r = d[name]
        require(r["status"] == "PASS" and r["payloads_included"] is False, "missing scoped QEMU result")
        require(set(r["checks"]) == fields and all(v is True for v in r["checks"].values()), "required QEMU check absent or failed")
        require(all(re.fullmatch(r"[0-9a-f]{64}", r[k]) for k in ["result_sha256","serial_sha256"]), "invalid evidence hash")
    b = d["next_blocker"]
    require(b["id"] == "B4_IOKIT_PLATFORM_DRIVER" and b["stage"] == "IOKIT_MATCHING_REACHED", "B4 snapshot changed")
    require(b["panic"] == 'Unable to find driver for this platform: "qemu,virt".' and b["valid_platform_expert_initialized"] is False, "matching is not successful Platform Expert init")
    require(d["hardware"] == HARDWARE, "hardware maturity changed")
    m = d["monocypher"]
    require(m["used_in_p10"] == "4.0.2" and m["kernel_role"] == "VERIFICATION_NOT_SIGNING_AS_DOCUMENTED", "research component scope changed")
    require(m["upstream_release"] == "4.0.3" and m["upstream_release_date"] == "2026-06-15", "upstream record changed")
    require(m["incorporated_code_impact"] == "INDEPENDENT_TECHNICAL_REVIEW_REQUIRED", "unreviewed impact cannot be decided")
    require(all(m[k] is False for k in ["code_imported_in_RC2","upgrade_performed","vulnerable_implementation_claimed","audited_implementation_claimed"]), "unsupported component or audit claim")
    require(d["p09"]["reexecuted_in_RC2"] is False, "P0.9 not reexecuted here")
    return d

def load(root):
    root = root.resolve()
    lock = read(root / LOCK)
    require(lock["version"] == "W1-minimum-1" and lock["review_required_for_changes"] is True, "reviewed consistency lock required")
    required = {SOURCE, SCHEMA, "scripts/current_status.py"}
    require(set(lock["files"]) == required, "exact source/schema/derivation pins required")
    for n, expected in lock["files"].items():
        p = (root / n).resolve()
        require(p.is_relative_to(root) and p.is_file(), "local pin escapes candidate")
        require(sha(p.read_bytes()) == expected, "current-status admission hash mismatch: " + n)
    return validate_projection(read(root / SOURCE)), lock["files"]

def derive(d, pins):
    return {
        "$schema": "../" + SCHEMA, "schema_version":"1.0", "release":"W4-RC2",
        "as_of":d["frozen_at"], "source_commit":PIN, "input_pins":pins,
        "evidence_scope":"DOCUMENTARY_REFERENCE_NOT_REEXECUTED", "environment":"QEMU_VIRT_DEV",
        "p09":"UNIFIED_CRYPTO_REGRESSION_DOCUMENTED",
        "research_b3":d["p10"]["research_state"], "apple_ipados_security":"NOT_ESTABLISHED",
        "next_blocker":d["next_blocker"], "interface":{"declared_version":7,
          "policy":"RESTRICTED_RESEARCH_OPERATIONS","entitlements":False,"sep":False,"full_userland":False},
        "hardware":d["hardware"],
        "monocypher":{"research_version":"4.0.2","kernel_role":"VERIFICATION_NOT_SIGNING_AS_DOCUMENTED",
          "upstream_release":"4.0.3","release_date":"2026-06-15","impact":"INDEPENDENT_TECHNICAL_REVIEW_REQUIRED"},
        "publication_status":"PUBLICATION_BLOCKED", "decision_status":"HUMAN_DECISION_REQUIRED"
    }

def status_text(d):
    return f"""# Current status — RC2 snapshot 2026-11-02

Generated by scripts/current_status.py from pinned public documentary projections. Do not edit this file manually.
Source: P0.10 {PIN}. Later B4 commits do not update this RC automatically.

W1 remains the accepted documentary foundation; W2/W3 remains host-tested/ARM64-compiled.
Historical snapshots and W4 reports retain their original baseline states.

P0.9: unified cryptography in the DEV kernel, QEMU regression and scoped security tests documented.
P0.10: B3_RESEARCH_PROVIDER_RUNTIME_VERIFIED in QEMU_VIRT DEV with an independent research provider, authenticated PNRA-R1 trust cache and a project research root separate from Apple.
The pinned positive authentication and unauthorized-signer rejection records PASS. No QEMU was executed for RC2.

This is not APPLE_IPADOS_SECURITY_COMPATIBLE. The research Image4 interface declares version 7 with restricted operations/refusals.
No Apple entitlements, SEP or complete userland support. No Apple authority or security equivalence is established.

Next blocker in this snapshot: B4_IOKIT_PLATFORM_DRIVER.
Panic: Unable to find driver for this platform: "qemu,virt".
IOKit matching was reached; valid Platform Expert initialization was not demonstrated.

Moto G53 physical XNU boot and D1–D4: NOT_DEMONSTRATED. M9: HOST_ONLY. Sofiar: P1/UNKNOWN.
No physical GPU or SpringBoard progress is inferred.

P0.10 uses Monocypher 4.0.2 for verification, not kernel signing, as documented.
Upstream 4.0.3 (2026-06-15) contains a signing-timing correction; incorporated-code impact remains an independent technical review item.
No component code was imported or updated; no vulnerability or inherited audit approval is claimed. See docs/MONOCYPHER-REVIEW.md.

PUBLICATION_BLOCKED. HUMAN_DECISION_REQUIRED for licensing/attribution, confidential reporting and release authorization.
"""

def readme_block(d):
    return f"""{BEGIN}
## Engineering state — fixed RC2 snapshot

| Track | Evidence and limit |
|---|---|
| W1 / W2/W3 | Accepted documentary foundation; host tests and ARM64 compilation. Their preserved snapshots remain historical. |
| P0.9 | Unified DEV-kernel cryptography, QEMU regression and scoped security tests documented. |
| P0.10 | B3_RESEARCH_PROVIDER_RUNTIME_VERIFIED under a project research root with authenticated PNRA-R1; positive acceptance and unauthorized-signer rejection documented in QEMU DEV. |
| Apple compatibility | Not APPLE_IPADOS_SECURITY_COMPATIBLE. Research Image4 interface v7, restricted operations; no Apple entitlement, SEP or complete userland support. |
| New blocker | B4_IOKIT_PLATFORM_DRIVER: IOKit matching reached; a valid Platform Expert was not initialized. |
| Hardware / D1–D4 | No physical G53 XNU boot, physical GPU or SpringBoard demonstrated. M9 is host only; Sofiar remains P1/UNKNOWN. |

Panic: Unable to find driver for this platform: "qemu,virt".

This RC is fixed to P0.10 {PIN}; later B4 progress cannot update it automatically.
These are documentary references to earlier QEMU results, not new RC2 runtime observations.
Monocypher and security limitations: [P0.10 scope](docs/P0.10-STATUS.md) and [component review](docs/MONOCYPHER-REVIEW.md).

Target: iPadOS 18.7.8 / 22H352 / iPad7,11 / ARM64_ALL; XNU 11417.140.69 at 43a90889846e00bfb5cf1d255cdc0a701a1e05a4.
Penang: SM4350-AC/holi, Adreno 619. Accepted [ADR-0004](contracts/platform/foundation/docs/adr/ADR-0004-no-pac-userland-and-kernel-base.md) and [ADR-0006](contracts/platform/foundation/docs/adr/ADR-0006-delivery-scope-and-tracks.md) and Apple binary preservation remain unchanged.

PUBLICATION_BLOCKED; [human decisions](PUBLICATION-DECISIONS.md) remain pending.
{END}"""

def expected_files(root):
    d, pins = load(root)
    manifest = derive(d, pins)
    schema = read(root / SCHEMA)
    Draft202012Validator.check_schema(schema)
    Draft202012Validator(schema).validate(manifest)
    readme = (root / "README.md").read_text(encoding="utf-8")
    require(readme.count(BEGIN) == 1 and readme.count(END) == 1, "README status markers required")
    first, last = readme.index(BEGIN), readme.index(END) + len(END)
    require(first < last, "README marker order")
    return {"manifests/current-status.json":json.dumps(manifest,indent=2,ensure_ascii=False)+"\n",
            "STATUS.md":status_text(d),
            "README.md":readme[:first]+readme_block(d)+readme[last:]}

def check(root, write=False):
    outputs = expected_files(root)
    changed = []
    for n, text in outputs.items():
        p = root / n
        if not p.is_file() or p.read_text(encoding="utf-8") != text:
            changed.append(n)
            if write:
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text(text, encoding="utf-8")
    require(write or not changed, "generated status drift: " + ", ".join(changed))
    return {"status":"PASS","generated_files":list(outputs),"updated":changed if write else [],
            "source_commit":PIN,"runtime_reexecuted":False,"publication_status":"PUBLICATION_BLOCKED"}

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root",type=Path,default=ROOT)
    p.add_argument("--write",action="store_true")
    args = p.parse_args()
    try:
        result = check(args.root.resolve(), args.write)
    except (OSError, ValueError, KeyError, TypeError) as e:
        print(json.dumps({"status":"FAIL","error":str(e)}))
        return 1
    print(json.dumps(result))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())

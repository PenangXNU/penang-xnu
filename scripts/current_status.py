#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Derive the v0.1.0 current status from pinned public documentary projections; offline.

The projection (evidence/current-status-sources.json) references research records by commit and
SHA-256 without including their payloads. This script refuses any projection that moves a pin,
drops a negative control or promotes a research/QEMU/Android observation to Apple compatibility,
XNU-on-device, GPU or userland progress, then regenerates STATUS.md, the README status block and
manifests/current-status.json. Generated files are never edited by hand.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
from jsonschema import Draft202012Validator

ROOT = Path(__file__).resolve().parents[1]
RELEASE = "v0.1.0"
AS_OF = "2026-11-02"
P10_PIN = "afa7ef6acfb89bf5ae2f3d0c67e7a775096f39b0"
P11_PIN = "bed47e61df1edbda3cb26d868770b0df27ecb94f"
D1_PIN = "6838eaab4fd5ceefa9b9fd3f372500f2d393f969"
RC2_COMMIT = "9dee1e57e0ae2a81f0e89a7cceeb283f6abbfad0"
SOURCE = "evidence/current-status-sources.json"
SCHEMA = "schemas/current-status.schema.json"
LOCK = "evidence/current-status.lock.json"
BEGIN = "<!-- CURRENT_STATUS:BEGIN -->"
END = "<!-- CURRENT_STATUS:END -->"
EXPECTED = {
 "reports/P0.10-HANDOFF.md": (P10_PIN, "af5002117d4142358d20bf7f9b64874545a87068b7611f7f8dc1acabf9e0ac57"),
 "reports/P0.10-GATES.json": (P10_PIN, "c0518917aef62ad52af60f7dc86b25dce58fe5f9617c6d6602f3a2dc720fb164"),
 "reports/P0.10-TEST-RESULTS.json": (P10_PIN, "52d8f909881426b72c22925d30ac5f7f2b78ea73319e4c24eedc31bae26c5d3c"),
 "reports/p10/qemu-check-accept.json": (P10_PIN, "6a597f203f2f3be02b55efb2c613b8442e3243cbac73e24df491bc4f5dee6acb"),
 "reports/p10/qemu-check-reject.json": (P10_PIN, "9660abbb7501ad5067faba2df0409e3d2243b4240b0ad6e568f8a79e920da7ae"),
 "reports/P0.11-HANDOFF.md": (P11_PIN, "56925d5ed9df8850598be126417163c9fbfbd4fde0fbaad23f880c3406907d66"),
 "reports/P0.11-GATES.json": (P11_PIN, "23a69baff9a0f1c053f129a69176eb5729703e2861651450cd3523f646779178"),
 "reports/P0.11-TEST-RESULTS.json": (P11_PIN, "cbf8fb47f1a7bf1b4e28b22bb12650c9b60d743b550c6585932b0a6241dd28e3"),
 "reports/p11/qemu-check-positive.json": (P11_PIN, "1743418791fe0d79ba03d7e45ecd10ea28efcb3b756d75ea9c62dc6481030449"),
 "reports/p11/qemu-check-unknown-platform.json": (P11_PIN, "b338c288ba02602de0505625a5455d4944a54ec9579a74a158939ecaa2931acd"),
 "reports/p11/qemu-check-identity-refused.json": (P11_PIN, "449303d5c3d9048d704eeb496a239bf5d0c56413945942d8fd26f408dca56af5"),
 "contracts/p11/matching-contract.json": (P11_PIN, "f90df406f45c0cb2c99dccd385e5c2c14b1382e7eb2eafa7f86990ce9ef71191"),
 "reports/penang/D1-PENANG-FASTBOOT-BOOT-20261102.md": (D1_PIN, "efd2719c1e53d87b9118e48932a2820bfee6d9de8ccbb1c3b5fe8f05dcddfd98"),
 "contracts/penang/hw-facts-20261102.json": (D1_PIN, "6b104923b97fb9d63dbf11712cd193564c169e45556d107564a9c464cbe15f35"),
}
HARDWARE = {"Moto_G53_XNU_boot": "NOT_DEMONSTRATED", "D1_D4": "NOT_DEMONSTRATED",
            "M9": "HOST_ONLY", "Sofiar": "P1_UNKNOWN", "physical_GPU_progress": False,
            "SpringBoard_demonstrated": False}
COMMON11 = {"state_observed", "p09_crypto_lines_unchanged", "research_providers_registered_once",
            "research_trust_cache_loaded", "iokit_matching_started"}
CHECKS = {
 ("p10", "positive"): {"state_observed", "trust_cache_placed_by_loader", "p09_crypto_lines_unchanged",
    "prng_registered", "providers_registered_once", "order_crypto_then_research_then_trustcache",
    "b3_panic_absent", "research_tc_loader_reached", "static_trust_cache_loaded",
    "no_trust_cache_failure", "next_stop_recorded"},
 ("p10", "negative"): {"state_observed", "trust_cache_placed_by_loader", "p09_crypto_lines_unchanged",
    "prng_registered", "providers_registered_once", "order_crypto_then_research_then_trustcache",
    "b3_panic_absent", "research_tc_loader_reached", "unauthorized_module_refused",
    "fail_closed_guard", "nothing_loaded"},
 ("p11", "positive"): COMMON11 | {"device_tree_unchanged", "probe_accepts", "start_runs_and_returns_true",
    "registered_for_matching", "iopanicplatform_not_started", "b4_panic_absent", "no_refusal_lines"},
 ("p11", "unknown_platform"): COMMON11 | {"negative_control_declared", "fallback_panic",
    "iopanicplatform_started", "platform_expert_not_started", "personality_not_matched"},
 ("p11", "identity_refused"): COMMON11 | {"negative_control_declared", "fallback_panic",
    "iopanicplatform_started", "platform_expert_not_started", "probe_refused_with_defined_error",
    "refusal_before_fallback"},
}
NEXT_BLOCKER = {"id": "B5_PLATFORM_INTERRUPT_CONTROLLER", "stage": "QUIET_WAIT_BEFORE_BSD_INIT", "panic": None,
                "waits": ["cpu_boot_thread in PassthruInterruptController::waitForChildController",
                          "kernel_bootstrap_thread in ml_wait_max_cpus (kperf_init_early)"],
                "later_dependency": "IOPMGR"}
DEVICE = {"gate": "OBS-D1", "gate_source": "ADR-0005 operational gate 'D1'; not the ADR-0006 delivery criterion D1",
          "image": "BYTE_IDENTICAL_COPY_OF_THE_FLASHED_BOOT_A_WITH_THE_OWNER_ROOT_PATCH_NOT_FACTORY_IMAGE",
          "fastboot_boot_accepted": True, "android_booted": True, "kernel_arguments_identical_to_flash_boot": True,
          "partitions_and_slot_unchanged": True, "android_pstore_records_preserved": True,
          "xnu_on_device": "NOT_RUN", "tgran16": "UNKNOWN", "watchdog_at_handoff": "UNKNOWN",
          "raw_data_included": False}


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


def check_result(name, r, fields):
    require(r["status"] == "PASS" and r["payloads_included"] is False, "missing scoped QEMU result: " + name)
    require(set(r["checks"]) == fields and all(v is True for v in r["checks"].values()),
            "required QEMU check absent or failed: " + name)
    require(all(re.fullmatch(r"[0-9a-f]{64}", r[k]) for k in ["result_sha256", "serial_sha256"]),
            "invalid evidence hash: " + name)


def validate_projection(d):
    require(d["schema_version"] == "2.0" and d["release"] == RELEASE and d["frozen_at"] == AS_OF, "release is frozen")
    require(d["previous_snapshot"]["release"] == "W4-RC2" and d["previous_snapshot"]["commit"] == RC2_COMMIT,
            "RC2 history reference changed")
    require(d["commits"] == {"p10": P10_PIN, "p11": P11_PIN, "device_obs_d1": D1_PIN}, "research pins are frozen")
    require(d["runtime_reproduced_by_release"] is False, "documentary reference is not a new runtime test")
    require(d["evidence_kind"] == "DOCUMENTARY_REFERENCE_TO_QEMU_AND_DEVICE_RESULTS", "evidence scope changed")
    require(set(d["sources"]) == set(EXPECTED), "exact pinned documents required")
    for n, (commit, h) in EXPECTED.items():
        r = d["sources"][n]
        require(r["commit"] == commit and r["sha256"] == h and r["payload_included"] is False, "source identity changed: " + n)
    p = d["p10"]
    require(p["research_state"] == "B3_RESEARCH_PROVIDER_RUNTIME_VERIFIED", "research status changed")
    require(p["apple_ipados_security_compatible"] == "NOT_ESTABLISHED", "research is not Apple compatibility")
    require(p["trust_cache"] == "PNRA-R1_AUTHENTICATED" and p["trust_root"] == "PROJECT_RESEARCH_ROOT_SEPARATE_FROM_APPLE",
            "trust scope changed")
    require(p["image4_research_interface_version"] == 7 and p["operations"] == "RESTRICTED_DEFINED_REFUSALS",
            "interface scope changed")
    require(all(p[k] is False for k in ["apple_entitlements", "sep_support", "complete_userland_support"]),
            "unsupported security claim")
    q = d["p11"]
    require(q["research_state"] == "B4_RESEARCH_PLATFORM_EXPERT_RUNTIME_VERIFIED", "B4 research status changed")
    require(q["fallback_preserved"] == "IOPanicPlatform", "fallback must be preserved")
    require(q["apple_platform_identity_claimed"] is False and q["userland_executed"] is False,
            "unsupported platform or userland claim")
    require(set(q["not_provided"]) == {"NVRAM", "RTC", "interrupt controller", "CPU power manager",
                                       "restart hardware", "storage"}, "provided-service scope changed")
    for (group, name), fields in CHECKS.items():
        check_result(group + "." + name, d[group][name], fields)
    require(d["next_blocker"] == NEXT_BLOCKER, "next blocker changed")
    require(d["device_observability"] == DEVICE, "device observability scope changed")
    require(d["hardware"] == HARDWARE, "hardware maturity changed")
    m = d["monocypher"]
    require(m["used_in_p10"] == "4.0.2" and m["kernel_role"] == "VERIFICATION_NOT_SIGNING_AS_DOCUMENTED",
            "research component scope changed")
    require(m["upstream_release"] == "4.0.3" and m["upstream_release_date"] == "2026-06-15", "upstream record changed")
    require(m["incorporated_code_impact"] == "INDEPENDENT_TECHNICAL_REVIEW_REQUIRED", "unreviewed impact cannot be decided")
    require(all(m[k] is False for k in ["code_imported_in_release", "upgrade_performed",
                                        "vulnerable_implementation_claimed", "audited_implementation_claimed"]),
            "unsupported component or audit claim")
    require(d["p09"]["reexecuted_in_release"] is False, "P0.9 not reexecuted here")
    return d


def load(root):
    root = root.resolve()
    lock = read(root / LOCK)
    require(lock["version"] == "v0.1-1" and lock["review_required_for_changes"] is True, "reviewed consistency lock required")
    required = {SOURCE, SCHEMA, "scripts/current_status.py"}
    require(set(lock["files"]) == required, "exact source/schema/derivation pins required")
    for n, expected in lock["files"].items():
        p = (root / n).resolve()
        require(p.is_relative_to(root) and p.is_file(), "local pin escapes candidate")
        require(sha(p.read_bytes()) == expected, "current-status admission hash mismatch: " + n)
    return validate_projection(read(root / SOURCE)), lock["files"]


def derive(d, pins):
    return {
        "$schema": "../" + SCHEMA, "schema_version": "2.0", "release": RELEASE, "as_of": AS_OF,
        "research_commits": d["commits"], "input_pins": pins,
        "evidence_scope": "DOCUMENTARY_REFERENCE_NOT_REEXECUTED", "environment": "QEMU_VIRT_DEV",
        "p09": "UNIFIED_CRYPTO_REGRESSION_DOCUMENTED",
        "research_b3": d["p10"]["research_state"], "research_b4": d["p11"]["research_state"],
        "apple_ipados_security": "NOT_ESTABLISHED", "next_blocker": d["next_blocker"],
        "interface": {"declared_version": 7, "policy": "RESTRICTED_RESEARCH_OPERATIONS",
                      "entitlements": False, "sep": False, "full_userland": False},
        "device_observability": d["device_observability"], "hardware": d["hardware"],
        "monocypher": {"research_version": "4.0.2", "kernel_role": "VERIFICATION_NOT_SIGNING_AS_DOCUMENTED",
                       "upstream_release": "4.0.3", "release_date": "2026-06-15",
                       "impact": "INDEPENDENT_TECHNICAL_REVIEW_REQUIRED"},
        "publication_status": "OWNER_DECISIONS_RECORDED",
        "decision_status": "PUBLIC_VISIBILITY_AFTER_INDEPENDENT_REVIEW",
    }


def status_text(d):
    return f"""# Current status — v0.1.0 (2026-11-02)

Generated by scripts/current_status.py from pinned public documentary projections. Do not edit this file manually.
Research pins: P0.10 {P10_PIN}, P0.11 {P11_PIN}, OBS-D1 {D1_PIN}. Later research commits do not update this release automatically.
The earlier W4-RC2 snapshot ({RC2_COMMIT}) remains in Git history and in the W4-RC2 records.

## QEMU_VIRT research kernel (XNU 11417.140.69, DEV, non-PAC)

- P0.9: unified cryptography in the DEV kernel; QEMU regression and scoped security tests documented.
- P0.10: B3_RESEARCH_PROVIDER_RUNTIME_VERIFIED. Independent research provider, authenticated PNRA-R1 trust cache, project research root separate from Apple. Authorized-module acceptance and unauthorized-signer rejection PASS.
- P0.11: B4_RESEARCH_PLATFORM_EXPERT_RUNTIME_VERIFIED. A built-in research Platform Expert for "qemu,virt" is selected by normal IOKit matching, starts and registers; IOPanicPlatform remains the fallback. Positive run and two negative controls (unknown platform, identity mismatch) PASS. No userland was executed.

Next blocker: B5_PLATFORM_INTERRUPT_CONTROLLER (no panic; quiet wait before bsd_init).
- cpu_boot_thread waits in PassthruInterruptController::waitForChildController for a platform interrupt-controller driver.
- kernel_bootstrap_thread waits in ml_wait_max_cpus (kperf_init_early).
- Later dependency: IOPMGR.

This is not APPLE_IPADOS_SECURITY_COMPATIBLE. The research Image4 interface declares version 7 with restricted operations/refusals.
No Apple entitlements, SEP or complete userland support. No Apple authority, platform identity or security equivalence is established.

## Moto G53 (penang)

OBS-D1 (ADR-0005 operational gate; not the ADR-0006 delivery criterion D1): the bootloader accepted `fastboot boot` of a byte-identical copy of the flashed boot image (owner's root patch, not a factory image). Android booted with unchanged kernel arguments; partitions and slot were unchanged; Android pstore records survived the path.
XNU on the device: NOT_RUN. TGRAN16 and watchdog state at handoff: UNKNOWN. No raw device data is published.

Moto G53 XNU boot and D1–D4: NOT_DEMONSTRATED. M9: HOST_ONLY. Sofiar: P1/UNKNOWN.
No physical GPU or SpringBoard progress is inferred.

## Components

P0.10 uses Monocypher 4.0.2 for verification, not kernel signing, as documented.
Upstream 4.0.3 (2026-06-15) contains a signing-timing correction; incorporated-code impact remains an independent technical review item.
See docs/MONOCYPHER-REVIEW.md.

Owner decisions are recorded in PUBLICATION-DECISIONS.md. Public visibility follows an independent review.
"""


def readme_block(d):
    return f"""{BEGIN}
## Current state — v0.1.0

| Track | Evidence and limit |
|---|---|
| Kernel (QEMU_VIRT, research) | XNU 11417.140.69 DEV build boots in QEMU through IOKit matching. Research security provider (B3) and research Platform Expert (B4) verified with positive and negative controls. |
| Next blocker | B5_PLATFORM_INTERRUPT_CONTROLLER: quiet wait before bsd_init; an interrupt-controller driver, then IOPMGR, are needed. |
| Apple compatibility | Not APPLE_IPADOS_SECURITY_COMPATIBLE: own research trust root, no Apple entitlements, SEP or userland yet. |
| Moto G53 (penang) | Bootloader accepts RAM boot (OBS-D1, Android image). XNU on the device: NOT_RUN. |
| D1–D4 (ADR-0006) | NOT_DEMONSTRATED. M9 graphics compiler is host only; Sofiar remains P1/UNKNOWN. |

Details: [STATUS.md](STATUS.md), [P0.11](docs/P0.11-STATUS.md), [device observability](docs/DEVICE-OBSERVABILITY.md), [P0.10](docs/P0.10-STATUS.md).
These are documentary references to pinned research records (P0.11 {P11_PIN[:7]}, OBS-D1 {D1_PIN[:7]}), not runtime observations made by this release.

Target: iPadOS 18.7.8 / 22H352 / iPad7,11 / ARM64_ALL (bring your own IPSW); XNU 11417.140.69 at 43a90889846e00bfb5cf1d255cdc0a701a1e05a4.
Penang: SM4350-AC/holi, Adreno 619. Accepted [ADR-0004](contracts/platform/foundation/docs/adr/ADR-0004-no-pac-userland-and-kernel-base.md), [ADR-0006](contracts/platform/foundation/docs/adr/ADR-0006-delivery-scope-and-tracks.md) and [ADR-0007](docs/adr/ADR-0007-original-experience.md).
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
    return {"manifests/current-status.json": json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
            "STATUS.md": status_text(d),
            "README.md": readme[:first] + readme_block(d) + readme[last:]}


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
    return {"status": "PASS", "generated_files": list(outputs), "updated": changed if write else [],
            "release": RELEASE, "research_commits": {"p10": P10_PIN, "p11": P11_PIN, "device_obs_d1": D1_PIN},
            "runtime_reexecuted": False}


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--root", type=Path, default=ROOT)
    p.add_argument("--write", action="store_true")
    args = p.parse_args()
    try:
        result = check(args.root.resolve(), args.write)
    except (OSError, ValueError, KeyError, TypeError) as e:
        print(json.dumps({"status": "FAIL", "error": str(e)}))
        return 1
    print(json.dumps(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

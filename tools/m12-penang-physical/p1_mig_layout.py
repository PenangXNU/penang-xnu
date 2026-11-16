# SPDX-License-Identifier: Apache-2.0
"""Route separately generated MIG interfaces without mixing server ABIs.

OSFMK uses its private kernel servers before its exported interfaces. IOKit
imports the exported OSFMK interfaces; an identically named private server
header can import MACH_KERNEL_PRIVATE types that are unavailable to IOKit.
This validates generation receipts and bytes, not IPC runtime behavior.
"""
import hashlib
import json
from pathlib import Path
import re
from p1_native_inputs import generated_path

XNU_COMMIT = "43a90889846e00bfb5cf1d255cdc0a701a1e05a4"
MIG_COMMIT = "619db33806b11b58fa14078bc7a811958312e52d"
MIG_COMMITS = {MIG_COMMIT, "c71d2d72f48995baaea76148f61002e5299841de"}
NATIVE_DEPLOYMENT_TARGET = "15.6"
ROLES = {
    "exported": ("mach", {"exported-user", "exported-server", "kernel-user"}),
    "kernel": ("mach", {"kernel-server"}),
    "device": ("device", {"kernel-server"}),
    "notification": ("UserNotification", {"kernel-user", "kernel-server"}),
}
OPTIONAL_ROLES = {r: (r, {"kernel-user"}) for r in ("gssd", "kextd", "lockd")}


def load_layout(roots):
    if (not set(ROLES) <= set(roots) <= (set(ROLES) | set(OPTIONAL_ROLES))
            or any(not isinstance(p, (str, Path)) for p in roots.values())):
        raise ValueError("all four named MIG roots required")
    admitted, receipts = {}, {}
    identity = None
    for role, (subsystem, modes) in {**ROLES, **OPTIONAL_ROLES}.items():
        if role not in roots:
            continue
        root = Path(roots[role]).resolve(strict=True)
        receipt = root / "results.json"
        if not root.is_dir() or receipt.is_symlink() or receipt.stat().st_size > 8 * 1024 * 1024:
            raise ValueError("invalid MIG generation receipt")
        data = json.loads(receipt.read_text())
        if not isinstance(data, dict):
            raise ValueError("MIG receipt must be an object")
        if (data.get("scope") != "HOST_ONLY_REAL_MIG_GENERATION"
                or data.get("status") not in ("GENERATED_HEADERS_ONLY_NOT_IPC_RUNTIME", "GENERATED_INTERFACES_NOT_IPC_RUNTIME")
                or data.get("xnu_commit") != XNU_COMMIT
                or data.get("generator_source_commit") not in MIG_COMMITS
                or data.get("native_deployment_target") != NATIVE_DEPLOYMENT_TARGET
                or data.get("subsystem_directory") != subsystem):
            raise ValueError("wrong MIG provenance or role")
        current = (data.get("generator_sha256"), data.get("osfmk_make_sha256"), data.get("config_defines"),
                   data["generator_source_commit"])
        if not all(isinstance(v, str) and re.fullmatch(r"[0-9a-f]{64}", v) for v in current[:2]):
            raise ValueError("missing MIG generation identity")
        if not isinstance(current[2], list) or not current[2] or any(not isinstance(v, str) for v in current[2]):
            raise ValueError("missing MIG configuration")
        if identity is not None and current != identity:
            raise ValueError("MIG roots generated with different binary or configuration")
        identity = current
        interfaces = data.get("interfaces")
        if not isinstance(interfaces, list) or not 0 < len(interfaces) <= 4096:
            raise ValueError("empty MIG interface inventory")
        seen = set()
        implementations = set()
        for item in interfaces:
            if not isinstance(item, dict):
                raise ValueError("MIG interface must be an object")
            name = item.get("interface")
            if (not isinstance(name, str) or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name)
                    or name in seen or item.get("mode") not in modes):
                raise ValueError("invalid, duplicate or wrong-mode MIG interface")
            seen.add(name)
            header = root / subsystem / (name + ".h")
            if (header.is_symlink() or header.resolve(strict=True) != header
                    or not header.is_file() or not 0 < header.stat().st_size <= 8 * 1024 * 1024
                    or hashlib.sha256(header.read_bytes()).hexdigest() != item.get("header_sha256")):
                raise ValueError("MIG header changed or escaped its root")
            implementation = item.get("implementation")
            if implementation is not None:
                if (not isinstance(implementation, dict) or not isinstance(implementation.get("path"), str)
                        or not re.fullmatch(re.escape(subsystem) + r"/[A-Za-z_][A-Za-z0-9_]*\.c", implementation["path"])):
                    raise ValueError("invalid MIG implementation inventory")
                path = generated_path(root, implementation["path"])
                if (path.stat().st_size != implementation.get("size")
                        or hashlib.sha256(path.read_bytes()).hexdigest() != implementation.get("sha256")):
                    raise ValueError("MIG implementation changed")
                if implementation["path"] in implementations:
                    raise ValueError("duplicate MIG implementation")
                implementations.add(implementation["path"])
        # Unlisted headers would also be visible to the compiler via -I.
        if {p.relative_to(root).as_posix() for p in root.rglob("*.h")} != {
                subsystem + "/" + name + ".h" for name in seen}:
            raise ValueError("unlisted MIG header")
        if {p.relative_to(root).as_posix() for p in (root / subsystem).rglob("*.c")} != implementations:
            raise ValueError("unlisted MIG implementation")
        admitted[role] = root
        receipts[role] = hashlib.sha256(receipt.read_bytes()).hexdigest()
    if len(set(admitted.values())) != len(admitted):
        raise ValueError("MIG roles must have distinct roots")
    return admitted, receipts


def component_paths(component, roots):
    if component == "osfmk":
        order = ("kernel", "exported", "device", "notification")
    elif component in ("iokit", "bsd", "libkern", "pexpert", "security", "libsa"):
        order = ("exported", "notification")
    else:
        raise ValueError("unsupported MIG consumer component")
    return [roots[role] for role in (*order, *OPTIONAL_ROLES) if role in roots]

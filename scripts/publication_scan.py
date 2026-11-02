#!/usr/bin/env python3
"""Bounded public tree + all local Git-object checks. No network and no writes."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess

IGNORED = {".git", ".build", ".venv", "__pycache__", "target"}
FORBIDDEN = {".agents", ".codex", ".sm", "device-logs", "firmware-images", "rom-work"}
BINARY_SUFFIXES = {".zip", ".xz", ".gz", ".deb", ".dmg", ".ipsw", ".img", ".bin", ".dtb",
                   ".dtbo", ".elf", ".o", ".a", ".so", ".dll", ".exe", ".p12", ".pfx", ".key"}
PATTERNS = {
    "PRIVATE_PATH": re.compile(r"(?:/(?:home|Users)/|/mnt/[a-z]/Users/)[A-Za-z0-9_.-]+/|[A-Za-z]:[\\/]+Users[\\/]+[A-Za-z0-9_.-]+[\\/]"),
    "PRIVATE_KEY": re.compile(r"-----BEGIN (?:[A-Z0-9 ]+ )?PRIVATE KEY-----"),
    "GITHUB_TOKEN": re.compile(r"gh[pousr]_[A-Za-z0-9]{25,}|github_pat_[A-Za-z0-9_]{30,}"),
    "SERVICE_TOKEN": re.compile(r"sk-(?:proj-)?[A-Za-z0-9_-]{24,}|AKIA[A-Z0-9]{16}"),
    "CREDENTIAL_ASSIGNMENT": re.compile(r"""(?im)^\s*(?:password|api_key|access_token|secret_key)\s*[:=]\s*["']?[A-Za-z0-9_+/=-]{16,}"""),
}

def digest(data):
    return hashlib.sha256(data).hexdigest()

def check_content(name, data):
    issues = []
    if len(data) > 8 * 1024 * 1024 or b"\x00" in data:
        return [{"code": "NON_TEXT_OR_OVERSIZE", "path": name}]
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        return [{"code": "NON_UTF8", "path": name}]
    for code, pattern in PATTERNS.items():
        if pattern.search(text):
            issues.append({"code": code, "path": name})
    return issues

def check_path(name):
    p = PurePosixPath(name)
    if p.is_absolute() or ".." in p.parts or any(x in FORBIDDEN for x in p.parts):
        return [{"code": "FORBIDDEN_PATH", "path": name}]
    if p.suffix.lower() in BINARY_SUFFIXES or p.name in {".env", ".netrc", "id_rsa", "id_ed25519"}:
        return [{"code": "FORBIDDEN_FILE_TYPE", "path": name}]
    return []

def public_files(root):
    files = []
    links = []
    for base, dirs, names in os.walk(root, followlinks=False):
        for n in list(dirs):
            p = Path(base) / n
            if p.is_symlink():
                links.append(p.relative_to(root).as_posix())
                dirs.remove(n)
            elif n in IGNORED:
                dirs.remove(n)
        for n in names:
            p = Path(base) / n
            if p.is_symlink():
                links.append(p.relative_to(root).as_posix())
            else:
                files.append(p)
    return files, links

def git(root, *args):
    return subprocess.check_output(["git", "--no-optional-locks", "-C", str(root), *args],
                                   env={**os.environ, "GIT_OPTIONAL_LOCKS": "0"})

def scan(root, history=True):
    root = root.resolve()
    issues = []
    files, links = public_files(root)
    issues.extend({"code": "SYMLINK", "path": n} for n in links)
    names = {p.relative_to(root).as_posix() for p in files}
    try:
        inventory = json.loads((root / "audit/public-files.json").read_text(encoding="utf-8"))
        allowed = set(inventory["files"])
        for n in sorted(names - allowed):
            issues.append({"code": "UNEXPECTED_FILE", "path": n})
        for n in sorted(allowed - names):
            issues.append({"code": "MISSING_FILE", "path": n})
        for n, h in inventory.get("content_pins", {}).items():
            p = root / n
            if not p.is_file() or digest(p.read_bytes()) != h:
                issues.append({"code": "CONTENT_PIN", "path": n})
    except (OSError, ValueError, KeyError, TypeError):
        allowed = names
        issues.append({"code": "INVENTORY_INVALID", "path": "audit/public-files.json"})
    for p in files:
        name = p.relative_to(root).as_posix()
        issues += check_path(name)
        issues += check_content(name, p.read_bytes())
        if p.suffix == ".md":
            text = p.read_text(encoding="utf-8")
            for target in re.findall(r"\[[^\]\n]+\]\(([^)\n]+)\)", text):
                if re.match(r"^(?:https?://|mailto:|#)", target):
                    continue
                target = target.split("#", 1)[0]
                if target and not (p.parent / target).is_file():
                    issues.append({"code": "BROKEN_LOCAL_LINK", "path": name, "target": target})
    history_state = "NOT_TESTED"
    objects = commits = 0
    if history and (root / ".git").exists():
        try:
            object_lines = git(root, "cat-file", "--batch-all-objects",
                               "--batch-check=%(objectname) %(objecttype)").decode().splitlines()
            for line in object_lines:
                oid, kind = line.split()
                if kind in {"blob", "commit", "tag"}:
                    issues += check_content("git-object:" + oid, git(root, "cat-file", kind, oid))
                    objects += 1
            commit_ids = git(root, "rev-list", "--all").decode().splitlines()
            commits = len(commit_ids)
            for oid in commit_ids:
                for entry in git(root, "ls-tree", "-rz", "--full-tree", oid).split(b"\x00"):
                    if not entry:
                        continue
                    metadata, raw_name = entry.split(b"\t", 1)
                    name = raw_name.decode("utf-8")
                    issues += check_path(name)
                    if name not in allowed:
                        issues.append({"code": "HISTORICAL_UNLISTED_FILE", "path": name, "commit": oid})
                    if metadata.startswith(b"120000"):
                        issues.append({"code": "HISTORICAL_SYMLINK", "path": name, "commit": oid})
            history_state = "PASS" if not issues else "FAIL"
        except (subprocess.CalledProcessError, ValueError, UnicodeError):
            issues.append({"code": "HISTORY_SCAN_FAILED", "path": ".git"})
            history_state = "FAIL"
    return {"status": "PASS" if not issues else "FAIL", "files_checked": len(files),
            "history_status": history_state, "git_content_objects_checked": objects,
            "commits_checked": commits, "issues": issues,
            "limits": "Known patterns and text/file policy; not exhaustive secret detection, legal title or runtime-security proof."}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--no-history", action="store_true")
    args = parser.parse_args()
    data = scan(args.root, not args.no_history)
    print(json.dumps(data, indent=2))
    return 0 if data["status"] == "PASS" else 1

if __name__ == "__main__":
    raise SystemExit(main())

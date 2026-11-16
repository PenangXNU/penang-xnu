#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Emit a pinned public pmap_cs.h include-scope delta; never apply it.

All direct TrustCache types in this header belong to PMAP_CS_PPL_MONITOR.
Keep that active branch dependent on the real API. No functions, declarations,
selectors, trust policy, startup calls or B4.2 guards are changed. Other direct
API clients remain dependencies. This is HOST_ONLY header hygiene, not a
TrustCache provider, admitted PMAP backend or security qualification.
"""
import argparse
import difflib
import json
from pathlib import Path

from p1_t1_entry import read, sha, write_exclusive

SOURCE = "osfmk/vm/pmap_cs.h"
SOURCE_SHA = "ee464f8d3b33eabede4b22500739b73ed2ceff8be7194d71dffae38d767b7bb8"
API = "#include <TrustCache/API.h>\n"
BLOCK = "#if PMAP_CS_PPL_MONITOR\n"
SELECTOR = """#if CONFIG_SPTM
#define PMAP_CS_PPL_MONITOR 0
#elif XNU_MONITOR
#define PMAP_CS_PPL_MONITOR 1
#else
#define PMAP_CS_PPL_MONITOR 0
#endif
"""


def relocate(source):
    if source.count(API) != 1 or source.count(BLOCK) != 1 or source.count(SELECTOR) != 1:
        raise ValueError("header dependency anchor drift")
    if not source.index(API) < source.index(SELECTOR) < source.index(BLOCK):
        raise ValueError("unexpected TrustCache include scope")
    return source.replace(API, "", 1).replace(BLOCK, BLOCK + "\n" + API, 1)


def render(root):
    root = root.resolve(strict=True)
    path = (root / SOURCE).resolve(strict=True)
    if not path.is_relative_to(root):
        raise ValueError("source escapes upstream root")
    data = read(path)
    if sha(data) != SOURCE_SHA:
        raise ValueError("pinned public pmap_cs.h SHA mismatch")
    source = data.decode("utf-8")
    updated = relocate(source)
    patch = "".join(difflib.unified_diff(source.splitlines(True), updated.splitlines(True),
                                       fromfile="a/" + SOURCE, tofile="b/" + SOURCE)).encode()
    return patch, {"scope": "HOST_ONLY", "source": SOURCE, "original_sha256": sha(data),
                   "result_sha256": sha(updated.encode()), "patch_sha256": sha(patch),
                   "changed_files": [SOURCE], "trustcache_provider": "NOT_PROVIDED",
                   "pmap_physical": "B42_BLOCKED_BACKEND_UNVERIFIED",
                   "native_xnu_full_link": "NOT_RUN", "xnu_device_entry": "NOT_RUN"}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source-root", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    a = p.parse_args()
    try:
        patch, report = render(a.source_root)
        write_exclusive(a.out, patch, (a.source_root,))
    except (OSError, UnicodeError, ValueError) as exc:
        p.exit(1, f"header dependency patch refused: {exc}\n")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

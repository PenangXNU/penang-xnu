#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Strict, independent JSON validator for air2ir3 outputs.

  json_check.py FILE [EXPR]

Rejects invalid UTF-8, duplicate object keys, NaN/Infinity and trailing data
(Python's json module, independent of the C emitter). If EXPR is given it is
evaluated with the parsed document bound to `j`; a false result fails.
Exit 0 = valid (and EXPR true), 1 = check failed, 2 = usage.
"""
import json
import sys


def no_dupes(pairs):
    keys = [k for k, _ in pairs]
    dup = {k for k in keys if keys.count(k) > 1}
    if dup:
        raise ValueError("duplicate keys: %s" % sorted(dup))
    return dict(pairs)


def no_const(name):
    raise ValueError("non-standard JSON constant %s" % name)


def main():
    if len(sys.argv) not in (2, 3):
        print(__doc__, file=sys.stderr)
        return 2
    try:
        with open(sys.argv[1], "rb") as f:
            text = f.read().decode("utf-8", "strict")
        j = json.loads(text, object_pairs_hook=no_dupes, parse_constant=no_const)
    except (OSError, UnicodeDecodeError, ValueError) as e:
        print("json_check: %s: %s" % (sys.argv[1], e), file=sys.stderr)
        return 1
    if len(sys.argv) == 3:
        try:
            ok = bool(eval(sys.argv[2], {"__builtins__": {"len": len, "all": all, "any": any,
                                                           "sum": sum, "set": set}}, {"j": j}))
        except Exception as e:  # noqa: BLE001 - report any assertion error as failure
            print("json_check: expression error: %s" % e, file=sys.stderr)
            return 1
        if not ok:
            print("json_check: expression false: %s" % sys.argv[2], file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

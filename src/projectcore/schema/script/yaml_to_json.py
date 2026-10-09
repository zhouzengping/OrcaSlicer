#!/usr/bin/env python3
"""Convert the schema YAMLs to JSON for format comparison.

Writes <name>.json next to each <name>.yaml under doc/schema/ (main files and
i18n/). Key order follows the YAML document order (JSON objects are unordered
by spec, but the serializer preserves insertion order for readability).

Run:  python scripts/schema_gen/yaml_to_json.py [file.yaml ...]
      (no args = convert every .yaml under doc/schema/)
"""
import json
import os
import sys

import yaml

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCHEMA_DIR = os.path.join(ROOT, "doc", "schema")


def convert(path):
    with open(path, "r", encoding="utf-8") as f:
        data = yaml.safe_load(f)
    out_path = os.path.splitext(path)[0] + ".json"
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        # ensure_ascii=False keeps zh_CN text human-readable in the diff
        json.dump(data, f, ensure_ascii=False, indent=2)
        f.write("\n")
    return out_path


def main():
    args = sys.argv[1:]
    paths = [os.path.join(ROOT, a) for a in args] if args else [
        os.path.join(dp, fn)
        for dp, _dn, fns in os.walk(SCHEMA_DIR)
        for fn in fns if fn.endswith(".yaml")
    ]
    for p in sorted(paths):
        if not os.path.isfile(p):
            print("not found: %s" % p)
            continue
        print("%s -> %s" % (os.path.relpath(p, ROOT), os.path.relpath(convert(p), ROOT)))


if __name__ == "__main__":
    main()

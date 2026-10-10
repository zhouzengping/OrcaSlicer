#!/usr/bin/env python3
"""Convert the schema YAMLs to JSON for format comparison.

Writes <name>.json next to each <name>.yaml under src/projectcore/schema/
(main files and i18n/). Order information lives in arrays (tabs/groups/keys),
which stay ordered in JSON as well; mapping fields are property bags whose
order is not guaranteed (the serializer preserves insertion order for
readability only).

Run:  python src/projectcore/schema/script/yaml_to_json.py [file.yaml ...]
      (no args = convert every .yaml under src/projectcore/schema/)
"""
import json
import os
import sys

import yaml

# script lives at <root>/src/projectcore/schema/script/ -- five dirname hops
# from __file__ land on the repository root
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))))
SCHEMA_DIR = os.path.join(ROOT, "src", "projectcore", "schema")


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

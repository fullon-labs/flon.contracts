#!/usr/bin/env python3
"""Compare read-only dao.gov snapshots across the one-time unit migration."""
import json
import sys
from pathlib import Path


before, after = (Path(arg) for arg in sys.argv[1:3])
names = sorted(path.name for path in before.glob("*.json") if path.name != "get_info.json")
if not names:
    raise SystemExit("no governance snapshots found")
if sorted(path.name for path in after.glob("*.json") if path.name != "get_info.json") != names:
    raise SystemExit("snapshot table set differs")


def relabel(value):
    if isinstance(value, dict):
        return {key: relabel(item) for key, item in value.items()}
    if isinstance(value, list):
        return [relabel(item) for item in value]
    if isinstance(value, str) and value.endswith(" GLON"):
        return value[:-4] + "POWER"
    if value == "8,GLON":
        return "8,POWER"
    return value


for name in names:
    old = json.loads((before / name).read_text())
    new = json.loads((after / name).read_text())
    if relabel(old) != new:
        raise SystemExit(f"unexpected governance table change: {name}")
print(json.dumps({"verified_tables": len(names), "result": "exact unit relabel only"}))

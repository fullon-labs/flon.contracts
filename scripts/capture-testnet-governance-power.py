#!/usr/bin/env python3
"""Read-only, chain-guarded backup of every dao.gov governance-power table."""
import json
import sys
import urllib.request
from decimal import Decimal
from pathlib import Path

CHAIN_ID = "18b4d5837d83a8f7203ea57cb7ab37b9cef59547ea307e70b1bf1103cb06043f"
RPC = "https://t4.flon.network"
opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

def post(path, body):
    data = json.dumps(body).encode()
    req = urllib.request.Request(RPC + path, data=data,
                                 headers={"Content-Type": "application/json"})
    with opener.open(req, timeout=15) as response:
        return json.load(response)

def table(name, scope="dao.gov"):
    result = post("/v1/chain/get_table_rows", {
        "json": True, "code": "dao.gov", "scope": scope,
        "table": name, "limit": 1000,
    })
    if result.get("more"):
        raise RuntimeError(f"pagination required for {name}/{scope}")
    return result

out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=False)
info = post("/v1/chain/get_info", {})
if info["chain_id"] != CHAIN_ID:
    raise RuntimeError("refusing a non-testnet chain")
(out / "get_info.json").write_text(json.dumps(info, indent=2) + "\n")
tables = {}
for name in ("global", "config", "powers", "riskconfigs", "policysnaps", "proposals"):
    tables[name] = table(name)
    (out / f"{name}.json").write_text(json.dumps(tables[name], indent=2) + "\n")
for power in tables["powers"]["rows"]:
    name = power["voter"]
    (out / f"checkpoints-{name}.json").write_text(
        json.dumps(table("checkpoints", name), indent=2) + "\n")
for proposal in tables["proposals"]["rows"]:
    scope = str(proposal["proposal_id"])
    for name in ("endorsements", "ballots"):
        (out / f"{name}-{scope}.json").write_text(
            json.dumps(table(name, scope), indent=2) + "\n")
total = Decimal(tables["global"]["rows"][0]["total_power"].split()[0])
power_sum = sum((Decimal(row["power"].split()[0]) for row in tables["powers"]["rows"]),
                Decimal(0))
if total != power_sum:
    raise RuntimeError(f"power conservation failed: {total} != {power_sum}")
print(json.dumps({"chain_id": CHAIN_ID, "head": info["head_block_num"],
                  "lib": info["last_irreversible_block_num"],
                  "powers": len(tables["powers"]["rows"]),
                  "proposals": len(tables["proposals"]["rows"]),
                  "total_power": str(total)}, indent=2))

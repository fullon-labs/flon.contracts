#!/usr/bin/env python3
"""Prepare a temporary testnet dao.gov build from a disposable source copy.

Never run this against the production checkout. The generated contract supports
the old system action during the rolling upgrade and is replaced immediately
after migrateunit succeeds.
"""
from pathlib import Path
import sys

root = Path(sys.argv[1]).resolve()
assert root.name == "migration-src" and "codex-power-clean" in str(root)
header = root / "contracts/dao.gov/include/dao.gov/dao.gov.hpp"
source = root / "contracts/dao.gov/src/dao.gov.cpp"
snippet = root / "scripts/testnet-power-unit-migration.cpp.inc"

h = header.read_text()
old = "   [[eosio::action]] void setpower(name voter, asset power);"
assert h.count(old) == 1
h = h.replace(old, old + "\n   [[eosio::action]] void syncpower(name voter);\n   [[eosio::action]] void migrateunit();")
header.write_text(h)

s = source.read_text()
assert s.count("#include <eosio/action.hpp>") == 1
s = s.replace("#include <eosio/action.hpp>",
              "#include <eosio/action.hpp>\n#include <flon.token/flon.token.hpp>")
old = "   require_auth(state.power_source);\n   update_power(voter, power, state);"
assert s.count(old) == 1
s = s.replace(old, """   require_auth(state.power_source);
   eosio::check(power.symbol.precision() == state.governance_symbol.precision() &&
                power.symbol.code() == eosio::symbol_code("POWER"),
                "POWER unit required");
   update_power(voter, asset(power.amount, state.governance_symbol), state);""")
old = "void dao_governance::update_power(name voter, asset power, global_state& state) {"
assert s.count(old) == 1
compat = """void dao_governance::syncpower(name voter) {
   auto state = require_global();
   eosio::check(state.governance_symbol.code() == eosio::symbol_code("GLON"),
                "legacy synchronization disabled after migration");
   eosio::check(eosio::is_account(voter), "voter account does not exist");
   eosio::token::accounts balances("flon.token"_n, voter.value);
   const auto balance = balances.find(state.governance_symbol.code().raw());
   const asset power = balance == balances.end()
      ? asset(0, state.governance_symbol) : balance->balance;
   update_power(voter, power, state);
}

"""
s = s.replace(old, compat + snippet.read_text() + "\n" + old)
source.write_text(s)

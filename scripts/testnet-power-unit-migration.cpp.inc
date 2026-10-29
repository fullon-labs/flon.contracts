// One-time testnet-only action. This file is intentionally not included by the
// production contract build. Deploy its temporary variant only to the verified
// testnet, run migrateunit, then replace it with the clean production contract.
void dao_governance::migrateunit() {
   auto state = require_global();
   require_auth(state.admin);
   eosio::check(state.governance_symbol.code() == eosio::symbol_code("GLON"),
                "legacy governance unit required");
   const symbol next(eosio::symbol_code("POWER"), state.governance_symbol.precision());
   auto relabel = [&](asset value) {
      eosio::check(value.symbol == state.governance_symbol,
                   "unexpected governance asset symbol");
      return asset(value.amount, next);
   };

   proposals_table proposals(get_self(), get_self().value);
   for (auto itr = proposals.begin(); itr != proposals.end(); ++itr) {
      const bool terminal = itr->status == defeated || itr->status == cancelled ||
                            itr->status == executed || itr->status == expired ||
                            (itr->status == succeeded && itr->actions.empty());
      eosio::check(terminal, "finish active proposals before migration");
   }

   config_singleton config(get_self(), get_self().value);
   if (config.exists()) {
      auto row = config.get();
      row.proposal_threshold = relabel(row.proposal_threshold);
      row.endorsement_threshold = relabel(row.endorsement_threshold);
      config.set(row, get_self());
   }
   riskconfigs_table risks(get_self(), get_self().value);
   for (auto itr = risks.begin(); itr != risks.end(); ++itr) {
      risks.modify(itr, eosio::same_payer, [&](auto& row) {
         row.min_total_power = relabel(row.min_total_power);
         row.proposal_threshold = relabel(row.proposal_threshold);
         row.endorsement_threshold = relabel(row.endorsement_threshold);
      });
   }
   powers_table powers(get_self(), get_self().value);
   for (auto itr = powers.begin(); itr != powers.end(); ++itr) {
      checkpoints_table checkpoints(get_self(), itr->voter.value);
      for (auto cp = checkpoints.begin(); cp != checkpoints.end(); ++cp) {
         checkpoints.modify(cp, eosio::same_payer, [&](auto& row) {
            row.power = relabel(row.power);
         });
      }
      powers.modify(itr, eosio::same_payer, [&](auto& row) {
         row.power = relabel(row.power);
      });
   }
   policysnaps_table snapshots(get_self(), get_self().value);
   for (auto itr = snapshots.begin(); itr != snapshots.end(); ++itr) {
      snapshots.modify(itr, eosio::same_payer, [&](auto& row) {
         row.endorsement_power = relabel(row.endorsement_power);
         row.min_total_power = relabel(row.min_total_power);
      });
   }
   for (auto itr = proposals.begin(); itr != proposals.end(); ++itr) {
      endorsements_table endorsements(get_self(), itr->proposal_id);
      for (auto rec = endorsements.begin(); rec != endorsements.end(); ++rec) {
         endorsements.modify(rec, eosio::same_payer, [&](auto& row) {
            row.power = relabel(row.power);
         });
      }
      ballots_table ballots(get_self(), itr->proposal_id);
      for (auto rec = ballots.begin(); rec != ballots.end(); ++rec) {
         ballots.modify(rec, eosio::same_payer, [&](auto& row) {
            row.power = relabel(row.power);
         });
      }
      proposals.modify(itr, eosio::same_payer, [&](auto& row) {
         row.snapshot_total_power = relabel(row.snapshot_total_power);
         row.for_votes = relabel(row.for_votes);
         row.against_votes = relabel(row.against_votes);
         row.abstain_votes = relabel(row.abstain_votes);
      });
   }
   state.total_power = relabel(state.total_power);
   state.governance_symbol = next;
   global_singleton(get_self(), get_self().value).set(state, get_self());
}

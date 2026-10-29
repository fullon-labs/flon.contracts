#include <dao.gov/dao.gov.hpp>

#include <eosio/action.hpp>

#include <limits>

namespace flon {

dao_governance::global_state dao_governance::require_global() const {
   global_singleton global(get_self(), get_self().value);
   eosio::check(global.exists(), "dao.gov is not initialized");
   return global.get();
}

dao_governance::governance_config dao_governance::require_config() const {
   config_singleton config(get_self(), get_self().value);
   eosio::check(config.exists(), "governance config is not set");
   return config.get();
}

void dao_governance::require_control(const global_state& state) const {
   if (has_auth(get_self())) {
      require_auth(get_self());
      return;
   }
   eosio::check(state.admin_enabled, "bootstrap administrator is disabled");
   require_auth(state.admin);
}

dao_governance::risk_config dao_governance::require_risk_config(
      uint8_t risk_level) const {
   riskconfigs_table configs(get_self(), get_self().value);
   const auto itr = configs.find(risk_level);
   eosio::check(itr != configs.end() && itr->enabled,
                "governance risk level is not configured");
   return *itr;
}

uint8_t dao_governance::action_risk(
      const vector<proposal_action>& actions) const {
   if (actions.empty()) return 1;
   actionpolicies_table policies(get_self(), get_self().value);
   const auto by_action = policies.get_index<"byaction"_n>();
   uint8_t highest = 0;
   for (const auto& action : actions) {
      const uint128_t key = (static_cast<uint128_t>(action.contract.value) << 64) |
                            action.action_name.value;
      const auto policy = by_action.find(key);
      eosio::check(policy != by_action.end() && policy->enabled,
                   "proposal action has no enabled risk policy");
      eosio::check(policy->risk_level >= 1 && policy->risk_level <= 4,
                   "proposal action has an invalid risk policy");
      if (policy->risk_level > highest) highest = policy->risk_level;
   }
   return highest;
}

checksum256 dao_governance::hash_actions(const vector<proposal_action>& actions) {
   if (actions.empty()) return checksum256{};
   const auto packed = eosio::pack(actions);
   return eosio::sha256(packed.data(), packed.size());
}

void dao_governance::validate_actions(const vector<proposal_action>& actions,
                                      const governance_config& config) const {
   eosio::check(actions.size() <= config.max_actions, "too many proposal actions");
   uint64_t total_bytes = 0;
   allowlist_table rules(get_self(), get_self().value);
   const auto by_action = rules.get_index<"byaction"_n>();
   for (const auto& action : actions) {
      eosio::check(action.contract.value != 0 && action.action_name.value != 0,
                   "invalid proposal action");
      eosio::check(action.data.size() <= config.max_action_bytes,
                   "proposal action data is too large");
      total_bytes += action.data.size();
      eosio::check(total_bytes <= config.max_total_action_bytes,
                   "total proposal action data is too large");
      const uint128_t key = (static_cast<uint128_t>(action.contract.value) << 64) |
                            action.action_name.value;
      const auto rule = by_action.find(key);
      eosio::check(rule != by_action.end() && rule->enabled,
                   "proposal action is not allowlisted");
      eosio::check(action.data.size() <= rule->max_data_bytes,
                   "proposal action exceeds allowlist data limit");
   }
   action_risk(actions);
}

void dao_governance::validate_uri(const string& value, const char* field, bool required) {
   if (required) eosio::check(!value.empty(), string(field) + " is required");
   eosio::check(value.size() <= 512, string(field) + " is too long");
}

asset dao_governance::current_power(name voter, const global_state& state) const {
   powers_table powers(get_self(), get_self().value);
   auto itr = powers.find(voter.value);
   return itr == powers.end() ? asset(0, state.governance_symbol) : itr->power;
}

asset dao_governance::snapshot_power(name voter, uint64_t sequence,
                                     symbol governance_symbol) const {
   checkpoints_table checkpoints(get_self(), voter.value);
   auto itr = checkpoints.upper_bound(sequence);
   if (itr == checkpoints.begin()) return asset(0, governance_symbol);
   --itr;
   return itr->power;
}

void dao_governance::init(name admin, name power_source, symbol governance_symbol) {
   require_auth(get_self());
   eosio::check(eosio::is_account(admin), "admin account does not exist");
   eosio::check(eosio::is_account(power_source), "power source account does not exist");
   eosio::check(governance_symbol.is_valid(), "invalid governance symbol");
   eosio::check(governance_symbol.code() == eosio::symbol_code("POWER"),
                "governance symbol must be POWER");
   global_singleton global(get_self(), get_self().value);
   eosio::check(!global.exists(), "already initialized");
   global.set({admin, power_source, governance_symbol, 0, 0, 0,
               asset(0, governance_symbol), true}, get_self());
}

void dao_governance::setconfig(asset proposal_threshold,
                               asset endorsement_threshold,
                               uint16_t required_endorsements,
                               uint32_t endorsement_seconds,
                               uint32_t voting_delay_seconds,
                               uint32_t voting_seconds,
                               uint16_t quorum_bps,
                               uint16_t approval_bps,
                               uint32_t timelock_seconds,
                               uint32_t execution_grace_seconds,
                               uint8_t max_actions,
                               uint32_t max_action_bytes,
                               uint32_t max_total_action_bytes) {
   auto state = require_global();
   require_control(state);
   eosio::check(proposal_threshold.symbol == state.governance_symbol &&
                endorsement_threshold.symbol == state.governance_symbol,
                "threshold symbol mismatch");
   eosio::check(proposal_threshold.amount >= 0 && endorsement_threshold.amount >= 0,
                "thresholds cannot be negative");
   eosio::check(required_endorsements > 0 && required_endorsements <= 1000,
                "invalid endorsement requirement");
   eosio::check(endorsement_seconds >= 60 && voting_seconds >= 60,
                "governance periods are too short");
   eosio::check(quorum_bps <= 10000 && approval_bps > 0 && approval_bps <= 10000,
                "invalid governance basis points");
   eosio::check(timelock_seconds >= 60 && execution_grace_seconds >= 60,
                "execution periods are too short");
   eosio::check(max_actions > 0 && max_actions <= 20,
                "invalid proposal action limit");
   eosio::check(max_action_bytes > 0 && max_action_bytes <= 16384 &&
                max_total_action_bytes >= max_action_bytes &&
                max_total_action_bytes <= 65536,
                "invalid proposal data limits");
   config_singleton(get_self(), get_self().value).set(
      {proposal_threshold, endorsement_threshold, required_endorsements,
       endorsement_seconds, voting_delay_seconds, voting_seconds,
       quorum_bps, approval_bps, timelock_seconds, execution_grace_seconds,
       max_actions, max_action_bytes, max_total_action_bytes}, get_self());
}

void dao_governance::setallow(name contract, name action_name,
                              name authorization_actor,
                              name authorization_permission,
                              uint32_t max_data_bytes, bool enabled) {
   auto state = require_global();
   const auto config = require_config();
   require_control(state);
   eosio::check(eosio::is_account(contract), "allowlist contract does not exist");
   eosio::check(action_name.value != 0, "allowlist action is required");
   eosio::check(eosio::is_account(authorization_actor),
                "authorization actor does not exist");
   eosio::check(authorization_permission.value != 0,
                "authorization permission is required");
   eosio::check(authorization_actor == get_self() ||
                authorization_actor == contract,
                "authorization actor must be dao.gov or the target contract");
   if (authorization_actor == contract && contract != get_self()) {
      eosio::check(authorization_permission != "owner"_n &&
                   authorization_permission != "active"_n,
                   "target contract owner or active permission is forbidden");
   }
   eosio::check(!(contract == get_self() && action_name == "execute"_n),
                "recursive governance execution is forbidden");
   eosio::check(!(contract == get_self() &&
                  (action_name == "setriskcfg"_n ||
                   action_name == "setactrisk"_n)),
                "governance safety policy cannot be allowlisted");
   eosio::check(max_data_bytes > 0 && max_data_bytes <= config.max_action_bytes,
                "invalid allowlist data limit");
   allowlist_table rules(get_self(), get_self().value);
   auto by_action = rules.get_index<"byaction"_n>();
   const uint128_t key = (static_cast<uint128_t>(contract.value) << 64) |
                         action_name.value;
   auto rule = by_action.find(key);
   if (rule == by_action.end()) {
      eosio::check(enabled, "allowlist rule not found");
      const uint64_t id = ++state.last_allow_id;
      rules.emplace(get_self(), [&](auto& row) {
         row = {id, contract, action_name, authorization_actor,
                authorization_permission, max_data_bytes, true};
      });
      global_singleton(get_self(), get_self().value).set(state, get_self());
   } else {
      eosio::check(rule->authorization_actor == authorization_actor &&
                   rule->authorization_permission == authorization_permission,
                   "allowlist authorization is immutable");
      by_action.modify(rule, eosio::same_payer, [&](auto& row) {
         row.max_data_bytes = max_data_bytes;
         row.enabled = enabled;
      });
   }
}

void dao_governance::setriskcfg(uint8_t risk_level,
                                asset min_total_power,
                                uint16_t quorum_bps,
                                uint16_t approval_bps,
                                uint16_t min_support_bps,
                                asset proposal_threshold,
                                asset endorsement_threshold,
                                uint16_t min_endorsement_bps,
                                uint16_t required_endorsements,
                                uint32_t endorsement_seconds,
                                uint32_t voting_delay_seconds,
                                uint32_t voting_seconds,
                                uint32_t timelock_seconds,
                                uint32_t execution_grace_seconds,
                                bool enabled) {
   require_auth("flon"_n);
   const auto state = require_global();
   eosio::check(risk_level >= 1 && risk_level <= 4, "risk level must be L1-L4");
   eosio::check(min_total_power.symbol == state.governance_symbol &&
                proposal_threshold.symbol == state.governance_symbol &&
                endorsement_threshold.symbol == state.governance_symbol,
                "risk threshold symbol mismatch");
   eosio::check(min_total_power.amount > 0 && proposal_threshold.amount >= 0 &&
                endorsement_threshold.amount >= 0,
                "invalid risk power thresholds");
   eosio::check(quorum_bps > 0 && quorum_bps <= 10000 &&
                approval_bps > 0 && approval_bps <= 10000 &&
                min_support_bps > 0 && min_support_bps <= 10000 &&
                min_endorsement_bps <= 10000,
                "invalid risk basis points");
   eosio::check(required_endorsements > 0 && required_endorsements <= 1000,
                "invalid risk endorsement requirement");
   eosio::check(endorsement_seconds >= 60 && voting_seconds >= 60,
                "risk governance periods are too short");
   eosio::check(timelock_seconds >= 60 && execution_grace_seconds >= 60,
                "risk execution periods are too short");

   riskconfigs_table configs(get_self(), get_self().value);
   for (auto itr = configs.begin(); itr != configs.end(); ++itr) {
      if (itr->risk_level == risk_level || !itr->enabled || !enabled) continue;
      const bool should_be_at_least = itr->risk_level < risk_level;
      const bool monotonic = should_be_at_least
         ? min_total_power >= itr->min_total_power &&
           quorum_bps >= itr->quorum_bps &&
           approval_bps >= itr->approval_bps &&
           min_support_bps >= itr->min_support_bps &&
           timelock_seconds >= itr->timelock_seconds
         : min_total_power <= itr->min_total_power &&
           quorum_bps <= itr->quorum_bps &&
           approval_bps <= itr->approval_bps &&
           min_support_bps <= itr->min_support_bps &&
           timelock_seconds <= itr->timelock_seconds;
      eosio::check(monotonic, "risk levels must be monotonically stricter");
   }

   auto itr = configs.find(risk_level);
   const risk_config value{risk_level, min_total_power, quorum_bps, approval_bps,
      min_support_bps, proposal_threshold, endorsement_threshold,
      min_endorsement_bps, required_endorsements, endorsement_seconds,
      voting_delay_seconds, voting_seconds, timelock_seconds,
      execution_grace_seconds, enabled};
   if (itr == configs.end()) {
      configs.emplace("flon"_n, [&](auto& row) { row = value; });
   } else {
      configs.modify(itr, eosio::same_payer, [&](auto& row) { row = value; });
   }
}

void dao_governance::setactrisk(name contract, name action_name,
                                uint8_t risk_level, bool enabled) {
   require_auth("flon"_n);
   eosio::check(eosio::is_account(contract), "risk policy contract does not exist");
   eosio::check(action_name.value != 0, "risk policy action is required");
   eosio::check(!(contract == get_self() &&
                  (action_name == "setriskcfg"_n ||
                   action_name == "setactrisk"_n)),
                "governance safety policy cannot govern itself");
   if (enabled) require_risk_config(risk_level);
   else eosio::check(risk_level >= 1 && risk_level <= 4,
                     "risk level must be L1-L4");

   actionpolicies_table policies(get_self(), get_self().value);
   auto by_action = policies.get_index<"byaction"_n>();
   const uint128_t key = (static_cast<uint128_t>(contract.value) << 64) |
                         action_name.value;
   auto itr = by_action.find(key);
   if (itr == by_action.end()) {
      eosio::check(enabled, "action risk policy not found");
      uint64_t id = policies.available_primary_key();
      if (id == 0) id = 1;
      policies.emplace("flon"_n, [&](auto& row) {
         row = {id, contract, action_name, risk_level, true};
      });
   } else {
      by_action.modify(itr, eosio::same_payer, [&](auto& row) {
         row.risk_level = risk_level;
         row.enabled = enabled;
      });
   }
}

void dao_governance::sealadmin() {
   auto state = require_global();
   eosio::check(state.admin_enabled, "bootstrap administrator is already disabled");
   require_auth(state.admin);
   state.admin_enabled = false;
   global_singleton(get_self(), get_self().value).set(state, get_self());
}

void dao_governance::setpower(name voter, asset power) {
   auto state = require_global();
   require_auth(state.power_source);
   update_power(voter, power, state);
}

void dao_governance::update_power(name voter, asset power, global_state& state) {
   eosio::check(eosio::is_account(voter), "voter account does not exist");
   eosio::check(power.symbol == state.governance_symbol && power.amount >= 0,
                "invalid governance power");
   powers_table powers(get_self(), get_self().value);
   auto itr = powers.find(voter.value);
   const int64_t previous = itr == powers.end() ? 0 : itr->power.amount;
   if (previous == power.amount) return;
   eosio::check(state.total_power.amount >= previous, "corrupt total governance power");
   const __int128 next_total = static_cast<__int128>(state.total_power.amount) - previous +
                               power.amount;
   eosio::check(next_total >= 0 &&
                next_total <= std::numeric_limits<int64_t>::max(),
                "governance power overflow");
   state.total_power.amount = static_cast<int64_t>(next_total);
   const uint64_t sequence = ++state.power_sequence;
   if (itr == powers.end()) {
      powers.emplace(get_self(), [&](auto& row) { row = {voter, power, sequence}; });
   } else {
      powers.modify(itr, eosio::same_payer, [&](auto& row) {
         row.power = power; row.last_sequence = sequence;
      });
   }
   checkpoints_table checkpoints(get_self(), voter.value);
   checkpoints.emplace(get_self(), [&](auto& row) { row = {sequence, power}; });
   global_singleton(get_self(), get_self().value).set(state, get_self());
}

void dao_governance::propose(name proposer, name proposal_type,
                             string title, string summary,
                             checksum256 content_hash,
                             string metadata_uri, string discussion_uri,
                             vector<proposal_action> actions) {
   require_auth(proposer);
   auto state = require_global();
   auto config = require_config();
   eosio::check(eosio::is_account(proposer), "proposer account does not exist");
   eosio::check(proposal_type.value != 0, "proposal type is required");
   eosio::check(!title.empty() && title.size() <= 128, "invalid proposal title");
   eosio::check(!summary.empty() && summary.size() <= 1024, "invalid proposal summary");
   eosio::check(content_hash != checksum256{}, "content hash is required");
   validate_uri(metadata_uri, "metadata URI", true);
   validate_uri(discussion_uri, "discussion URI", false);
   validate_actions(actions, config);
   const uint8_t risk_level = action_risk(actions);
   const auto risk = require_risk_config(risk_level);
   eosio::check(current_power(proposer, state) >= risk.proposal_threshold,
                "insufficient governance power to propose");
   const auto now = time_point_sec(eosio::current_time_point());
   proposals_table proposals(get_self(), get_self().value);
   const uint64_t proposal_id = ++state.last_proposal_id;
   proposals.emplace(proposer, [&](auto& row) {
      row.proposal_id = proposal_id;
      row.proposer = proposer;
      row.proposal_type = proposal_type;
      row.title = title;
      row.summary = summary;
      row.content_hash = content_hash;
      row.metadata_uri = metadata_uri;
      row.discussion_uri = discussion_uri;
      row.created_at = now;
      row.endorsement_end = time_point_sec(now.sec_since_epoch() + risk.endorsement_seconds);
      row.snapshot_total_power = asset(0, state.governance_symbol);
      row.for_votes = asset(0, state.governance_symbol);
      row.against_votes = asset(0, state.governance_symbol);
      row.abstain_votes = asset(0, state.governance_symbol);
      row.actions = std::move(actions);
      row.execution_hash = hash_actions(row.actions);
   });
   policysnaps_table snapshots(get_self(), get_self().value);
   snapshots.emplace(proposer, [&](auto& row) {
      row.proposal_id = proposal_id;
      row.risk_level = risk_level;
      row.endorsement_power = asset(0, state.governance_symbol);
      row.min_total_power = asset(0, state.governance_symbol);
   });
   global_singleton(get_self(), get_self().value).set(state, get_self());
}

void dao_governance::setforum(name proposer, uint64_t proposal_id,
                              string discussion_uri) {
   require_auth(proposer);
   validate_uri(discussion_uri, "discussion URI", true);
   proposals_table proposals(get_self(), get_self().value);
   auto proposal = proposals.require_find(proposal_id, "proposal not found");
   eosio::check(proposal->proposer == proposer, "only proposer may set discussion URI");
   eosio::check(proposal->status == endorsement, "discussion URI is frozen");
   proposals.modify(proposal, proposer, [&](auto& row) { row.discussion_uri = discussion_uri; });
}

void dao_governance::endorse(name endorser, uint64_t proposal_id) {
   require_auth(endorser);
   auto state = require_global();
   proposals_table proposals(get_self(), get_self().value);
   auto proposal = proposals.require_find(proposal_id, "proposal not found");
   eosio::check(proposal->status == endorsement, "proposal is not accepting endorsements");
   eosio::check(time_point_sec(eosio::current_time_point()) < proposal->endorsement_end,
                "endorsement period has ended");
   policysnaps_table snapshots(get_self(), get_self().value);
   auto snapshot = snapshots.require_find(proposal_id,
      "proposal predates risk policy; cancel and recreate it");
   const auto risk = require_risk_config(snapshot->risk_level);
   const asset power = current_power(endorser, state);
   eosio::check(power >= risk.endorsement_threshold,
                "insufficient governance power to endorse");
   endorsements_table endorsements(get_self(), proposal_id);
   eosio::check(endorsements.find(endorser.value) == endorsements.end(),
                "proposal already endorsed by account");
   endorsements.emplace(endorser, [&](auto& row) {
      row = {endorser, power, time_point_sec(eosio::current_time_point())};
   });
   proposals.modify(proposal, eosio::same_payer,
                    [&](auto& row) { ++row.endorsement_count; });
   snapshots.modify(snapshot, eosio::same_payer,
                    [&](auto& row) { row.endorsement_power += power; });
}

void dao_governance::activate(uint64_t proposal_id) {
   auto state = require_global();
   proposals_table proposals(get_self(), get_self().value);
   auto proposal = proposals.require_find(proposal_id, "proposal not found");
   eosio::check(proposal->status == endorsement, "proposal cannot be activated");
   const auto now = time_point_sec(eosio::current_time_point());
   eosio::check(now >= proposal->endorsement_end, "endorsement period is still active");
   policysnaps_table snapshots(get_self(), get_self().value);
   auto snapshot = snapshots.require_find(proposal_id,
      "proposal predates risk policy; cancel and recreate it");
   const uint8_t current_risk = action_risk(proposal->actions);
   const uint8_t frozen_risk = current_risk > snapshot->risk_level
      ? current_risk : snapshot->risk_level;
   const auto risk = require_risk_config(frozen_risk);
   eosio::check(state.total_power >= risk.min_total_power,
                "insufficient network governance power for this risk level");
   eosio::check(proposal->endorsement_count >= risk.required_endorsements,
                "insufficient endorsements");
   eosio::check(static_cast<__int128>(snapshot->endorsement_power.amount) * 10000 >=
                static_cast<__int128>(state.total_power.amount) *
                   risk.min_endorsement_bps,
                "insufficient endorsement power");
   proposals.modify(proposal, eosio::same_payer, [&](auto& row) {
      row.snapshot_sequence = state.power_sequence;
      row.snapshot_total_power = state.total_power;
      row.snapshot_quorum_bps = risk.quorum_bps;
      row.snapshot_approval_bps = risk.approval_bps;
      row.snapshot_timelock_seconds = risk.timelock_seconds;
      row.snapshot_execution_grace_seconds = risk.execution_grace_seconds;
      row.vote_start = time_point_sec(now.sec_since_epoch() + risk.voting_delay_seconds);
      row.vote_end = time_point_sec(row.vote_start.sec_since_epoch() + risk.voting_seconds);
      row.status = voting_delay;
   });
   snapshots.modify(snapshot, eosio::same_payer, [&](auto& row) {
      row.risk_level = frozen_risk;
      row.min_total_power = risk.min_total_power;
      row.quorum_bps = risk.quorum_bps;
      row.approval_bps = risk.approval_bps;
      row.min_support_bps = risk.min_support_bps;
      row.timelock_seconds = risk.timelock_seconds;
      row.execution_grace_seconds = risk.execution_grace_seconds;
      row.activated = true;
   });
}

void dao_governance::castvote(name voter, uint64_t proposal_id, uint8_t choice) {
   require_auth(voter);
   eosio::check(choice <= vote_abstain, "invalid vote choice");
   auto state = require_global();
   proposals_table proposals(get_self(), get_self().value);
   auto proposal = proposals.require_find(proposal_id, "proposal not found");
   const auto now = time_point_sec(eosio::current_time_point());
   eosio::check((proposal->status == voting_delay || proposal->status == voting) &&
                now >= proposal->vote_start && now < proposal->vote_end,
                "proposal is not open for voting");
   const asset power = snapshot_power(voter, proposal->snapshot_sequence,
                                      state.governance_symbol);
   eosio::check(power.amount > 0, "no governance power at proposal snapshot");
   ballots_table ballots(get_self(), proposal_id);
   eosio::check(ballots.find(voter.value) == ballots.end(), "account already voted");
   ballots.emplace(voter, [&](auto& row) {
      row = {voter, choice, power, now};
   });
   proposals.modify(proposal, eosio::same_payer, [&](auto& row) {
      row.status = voting;
      if (choice == vote_for) row.for_votes += power;
      else if (choice == vote_against) row.against_votes += power;
      else row.abstain_votes += power;
   });
}

void dao_governance::finalize(uint64_t proposal_id) {
   proposals_table proposals(get_self(), get_self().value);
   auto proposal = proposals.require_find(proposal_id, "proposal not found");
   eosio::check(proposal->status == voting_delay || proposal->status == voting,
                "proposal cannot be finalized");
   eosio::check(time_point_sec(eosio::current_time_point()) >= proposal->vote_end,
                "voting period is still active");
   const int64_t participation = proposal->for_votes.amount +
                                 proposal->against_votes.amount +
                                 proposal->abstain_votes.amount;
   const int64_t decisive = proposal->for_votes.amount + proposal->against_votes.amount;
   const bool quorum = static_cast<__int128>(participation) * 10000 >=
                       static_cast<__int128>(proposal->snapshot_total_power.amount) *
                       proposal->snapshot_quorum_bps;
   const bool approved = decisive > 0 &&
                         static_cast<__int128>(proposal->for_votes.amount) * 10000 >=
                         static_cast<__int128>(decisive) * proposal->snapshot_approval_bps;
   policysnaps_table snapshots(get_self(), get_self().value);
   const auto snapshot = snapshots.find(proposal_id);
   const bool absolute_support = snapshot == snapshots.end() ||
      (snapshot->activated &&
       static_cast<__int128>(proposal->for_votes.amount) * 10000 >=
       static_cast<__int128>(proposal->snapshot_total_power.amount) *
          snapshot->min_support_bps);
   proposals.modify(proposal, eosio::same_payer,
                    [&](auto& row) {
                       row.status = quorum && approved && absolute_support
                          ? succeeded : defeated;
                    });
}

void dao_governance::queue(uint64_t proposal_id) {
   const auto config = require_config();
   proposals_table proposals(get_self(), get_self().value);
   auto proposal = proposals.require_find(proposal_id, "proposal not found");
   eosio::check(proposal->status == succeeded, "proposal is not ready for timelock");
   eosio::check(!proposal->actions.empty(), "proposal has no executable actions");
   validate_actions(proposal->actions, config);
   eosio::check(hash_actions(proposal->actions) == proposal->execution_hash,
                "proposal execution hash mismatch");
   const auto now = time_point_sec(eosio::current_time_point());
   proposals.modify(proposal, eosio::same_payer, [&](auto& row) {
      row.execute_after = time_point_sec(now.sec_since_epoch() +
                                         row.snapshot_timelock_seconds);
      row.execute_before = time_point_sec(row.execute_after.sec_since_epoch() +
                                          row.snapshot_execution_grace_seconds);
      row.status = timelocked;
   });
}

void dao_governance::execute(uint64_t proposal_id) {
   const auto config = require_config();
   proposals_table proposals(get_self(), get_self().value);
   auto proposal = proposals.require_find(proposal_id, "proposal not found");
   eosio::check(proposal->status == timelocked, "proposal is not queued");
   const auto now = time_point_sec(eosio::current_time_point());
   eosio::check(now >= proposal->execute_after, "timelock has not expired");
   eosio::check(now <= proposal->execute_before, "proposal execution window has expired");
   validate_actions(proposal->actions, config);
   eosio::check(hash_actions(proposal->actions) == proposal->execution_hash,
                "proposal execution hash mismatch");

   const auto actions = proposal->actions;
   proposals.modify(proposal, eosio::same_payer, [&](auto& row) {
      row.status = executed;
      row.executed_at = now;
   });
   for (const auto& frozen : actions) {
      allowlist_table rules(get_self(), get_self().value);
      const auto by_action = rules.get_index<"byaction"_n>();
      const uint128_t key =
         (static_cast<uint128_t>(frozen.contract.value) << 64) |
         frozen.action_name.value;
      const auto rule = by_action.require_find(key,
                                               "proposal action is not allowlisted");
      eosio::action action;
      action.account = frozen.contract;
      action.name = frozen.action_name;
      action.authorization = {{rule->authorization_actor,
                               rule->authorization_permission}};
      action.data = frozen.data;
      action.send();
   }
}

void dao_governance::expire(uint64_t proposal_id) {
   proposals_table proposals(get_self(), get_self().value);
   auto proposal = proposals.require_find(proposal_id, "proposal not found");
   eosio::check(proposal->status == timelocked, "proposal is not queued");
   eosio::check(time_point_sec(eosio::current_time_point()) > proposal->execute_before,
                "proposal execution window is still active");
   proposals.modify(proposal, eosio::same_payer,
                    [&](auto& row) { row.status = expired; });
}

void dao_governance::cancel(name proposer, uint64_t proposal_id) {
   require_auth(proposer);
   proposals_table proposals(get_self(), get_self().value);
   auto proposal = proposals.require_find(proposal_id, "proposal not found");
   eosio::check(proposal->proposer == proposer, "only proposer may cancel proposal");
   eosio::check(proposal->status == endorsement, "proposal can no longer be cancelled");
   proposals.modify(proposal, proposer, [&](auto& row) { row.status = cancelled; });
}

} // namespace flon

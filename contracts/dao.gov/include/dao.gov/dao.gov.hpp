#pragma once

#include <eosio/asset.hpp>
#include <eosio/crypto.hpp>
#include <eosio/eosio.hpp>
#include <eosio/serialize.hpp>
#include <eosio/singleton.hpp>
#include <eosio/time.hpp>

#include <string>
#include <vector>

namespace flon {

using eosio::asset;
using eosio::checksum256;
using eosio::name;
using eosio::symbol;
using eosio::time_point_sec;
using std::string;
using std::vector;

class [[eosio::contract("dao.gov")]] dao_governance : public eosio::contract {
 public:
   using contract::contract;

   enum proposal_status : uint8_t {
      endorsement = 0,
      voting_delay = 1,
      voting = 2,
      defeated = 3,
      succeeded = 4,
      cancelled = 5,
      timelocked = 6,
      executed = 7,
      expired = 8
   };

   enum vote_choice : uint8_t {
      vote_against = 0,
      vote_for = 1,
      vote_abstain = 2
   };

   struct proposal_action {
      name contract;
      name action_name;
      vector<char> data;

      EOSLIB_SERIALIZE(proposal_action, (contract)(action_name)(data))
   };

   [[eosio::action]] void init(name admin, name power_source, symbol governance_symbol);
   [[eosio::action]] void setconfig(asset proposal_threshold,
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
                                    uint32_t max_total_action_bytes);
   [[eosio::action]] void setallow(name contract, name action_name,
                                   uint32_t max_data_bytes, bool enabled);
   [[eosio::action]] void sealadmin();
   [[eosio::action]] void setpower(name voter, asset power);
   [[eosio::action]] void syncpower(name voter);
   [[eosio::action]] void propose(name proposer, name proposal_type,
                                  string title, string summary,
                                  checksum256 content_hash,
                                  string metadata_uri, string discussion_uri,
                                  vector<proposal_action> actions);
   [[eosio::action]] void setforum(name proposer, uint64_t proposal_id,
                                   string discussion_uri);
   [[eosio::action]] void endorse(name endorser, uint64_t proposal_id);
   [[eosio::action]] void activate(uint64_t proposal_id);
   [[eosio::action]] void castvote(name voter, uint64_t proposal_id, uint8_t choice);
   [[eosio::action]] void finalize(uint64_t proposal_id);
   [[eosio::action]] void queue(uint64_t proposal_id);
   [[eosio::action]] void execute(uint64_t proposal_id);
   [[eosio::action]] void expire(uint64_t proposal_id);
   [[eosio::action]] void cancel(name proposer, uint64_t proposal_id);

 private:
   struct [[eosio::table("global")]] global_state {
      name admin;
      name power_source;
      symbol governance_symbol;
      uint64_t last_proposal_id = 0;
      uint64_t last_allow_id = 0;
      uint64_t power_sequence = 0;
      asset total_power;
      bool admin_enabled = true;
   };
   using global_singleton = eosio::singleton<"global"_n, global_state>;

   struct [[eosio::table("config")]] governance_config {
      asset proposal_threshold;
      asset endorsement_threshold;
      uint16_t required_endorsements = 1;
      uint32_t endorsement_seconds = 3 * 24 * 3600;
      uint32_t voting_delay_seconds = 24 * 3600;
      uint32_t voting_seconds = 3 * 24 * 3600;
      uint16_t quorum_bps = 1000;
      uint16_t approval_bps = 5000;
      uint32_t timelock_seconds = 2 * 24 * 3600;
      uint32_t execution_grace_seconds = 7 * 24 * 3600;
      uint8_t max_actions = 10;
      uint32_t max_action_bytes = 4096;
      uint32_t max_total_action_bytes = 16384;
   };
   using config_singleton = eosio::singleton<"config"_n, governance_config>;

   struct [[eosio::table]] voter_power {
      name voter;
      asset power;
      uint64_t last_sequence = 0;
      uint64_t primary_key() const { return voter.value; }
   };
   using powers_table = eosio::multi_index<"powers"_n, voter_power>;

   struct [[eosio::table]] power_checkpoint {
      uint64_t sequence;
      asset power;
      uint64_t primary_key() const { return sequence; }
   };
   using checkpoints_table = eosio::multi_index<"checkpoints"_n, power_checkpoint>;

   struct [[eosio::table]] proposal {
      uint64_t proposal_id;
      name proposer;
      name proposal_type;
      string title;
      string summary;
      checksum256 content_hash;
      string metadata_uri;
      string discussion_uri;
      time_point_sec created_at;
      time_point_sec endorsement_end;
      time_point_sec vote_start;
      time_point_sec vote_end;
      uint64_t snapshot_sequence = 0;
      asset snapshot_total_power;
      uint16_t snapshot_quorum_bps = 0;
      uint16_t snapshot_approval_bps = 0;
      uint32_t snapshot_timelock_seconds = 0;
      uint32_t snapshot_execution_grace_seconds = 0;
      uint16_t endorsement_count = 0;
      asset for_votes;
      asset against_votes;
      asset abstain_votes;
      vector<proposal_action> actions;
      checksum256 execution_hash;
      time_point_sec execute_after;
      time_point_sec execute_before;
      time_point_sec executed_at;
      uint8_t status = endorsement;
      uint64_t primary_key() const { return proposal_id; }
      uint64_t by_proposer() const { return proposer.value; }
   };
   using proposals_table = eosio::multi_index<"proposals"_n, proposal,
      eosio::indexed_by<"byproposer"_n,
         eosio::const_mem_fun<proposal, uint64_t, &proposal::by_proposer>>>;

   struct [[eosio::table]] allow_rule {
      uint64_t id;
      name contract;
      name action_name;
      uint32_t max_data_bytes = 0;
      bool enabled = false;
      uint64_t primary_key() const { return id; }
      uint128_t by_action() const {
         return (static_cast<uint128_t>(contract.value) << 64) | action_name.value;
      }
   };
   using allowlist_table = eosio::multi_index<"allowlist"_n, allow_rule,
      eosio::indexed_by<"byaction"_n,
         eosio::const_mem_fun<allow_rule, uint128_t, &allow_rule::by_action>>>;

   struct [[eosio::table]] endorsement_record {
      name endorser;
      asset power;
      time_point_sec endorsed_at;
      uint64_t primary_key() const { return endorser.value; }
   };
   using endorsements_table = eosio::multi_index<"endorsements"_n, endorsement_record>;

   struct [[eosio::table]] ballot {
      name voter;
      uint8_t choice;
      asset power;
      time_point_sec voted_at;
      uint64_t primary_key() const { return voter.value; }
   };
   using ballots_table = eosio::multi_index<"ballots"_n, ballot>;

   global_state require_global() const;
   governance_config require_config() const;
   asset current_power(name voter, const global_state& state) const;
   asset snapshot_power(name voter, uint64_t sequence, symbol governance_symbol) const;
   void update_power(name voter, asset power, global_state& state);
   void require_control(const global_state& state) const;
   void validate_actions(const vector<proposal_action>& actions,
                         const governance_config& config) const;
   static checksum256 hash_actions(const vector<proposal_action>& actions);
   static void validate_uri(const string& value, const char* field, bool required);
};

} // namespace flon

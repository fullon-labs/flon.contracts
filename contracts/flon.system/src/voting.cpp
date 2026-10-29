#include <eosio/crypto.hpp>
#include <eosio/datastream.hpp>
#include <eosio/eosio.hpp>
#include <eosio/multi_index.hpp>
#include <eosio/permission.hpp>
#include <eosio/privileged.hpp>
#include <eosio/serialize.hpp>
#include <eosio/singleton.hpp>

#include <flon.system/flon.system.hpp>
#include <flon.token/flon.token.hpp>
#include <voter.reward/voter.reward.hpp>

#include <type_traits>
#include <limits>
#include <set>
#include <algorithm>
#include <cmath>

namespace eosiosystem {

   using eosio::const_mem_fun;
   using eosio::current_time_point;
   using eosio::indexed_by;
   using eosio::microseconds;
   using eosio::seconds;
   using eosio::singleton;
   using std::to_string;

   uint64_t system_contract::next_vote_epoch() const {
      const auto now = current_time_point();
      if (_gstate.reward_started_time == time_point() || now < _gstate.reward_started_time) return 0;
      return current_reward_epoch(now) + 1;
   }

   static asset default_min_bp_self_stake(const symbol& core) {
      int64_t amount = 500'000;
      for (uint8_t i = 0; i < core.precision(); ++i) {
         check(amount <= std::numeric_limits<int64_t>::max() / 10,
               "core precision is too large for the default BP self-stake");
         amount *= 10;
      }
      return asset(amount, core);
   }

   static bp_stake_config get_bp_stake_config(name self, const symbol& core) {
      bp_stake_config_singleton config_table(self, self.value);
      if (config_table.exists()) return config_table.get();
      return bp_stake_config{false, default_min_bp_self_stake(core), 21 * 24 * 60 * 60};
   }

   bool system_contract::has_min_bp_self_stake(const name& producer,
                                                const bp_stake_config& config) const {
      bp_self_stake_table self_stakes(get_self(), get_self().value);
      const auto self_stake = self_stakes.find(producer.value);
      return self_stake != self_stakes.end() && self_stake->staked >= config.min_self_stake;
   }

   void system_contract::check_min_bp_self_stake(const name& producer,
                                                  const bp_stake_config& config) const {
      check(has_min_bp_self_stake(producer, config),
            "producer self-stake is below the minimum");
   }

   #ifdef ENABLE_VOTING_PRODUCER
   void system_contract::register_producer(  const name& producer,
                                             const block_signing_authority& producer_authority,
                                             const string& url,
                                             uint16_t location,
                                             optional<uint32_t> reward_shared_ratio,
                                             const binary_extension<string>& status_url ) {

      check( url.size() < 512, "url too long" );
      if (status_url.has_value()) {
         check(status_url.value().size() < 512, "status_url too long");
      }
      if (reward_shared_ratio) {
         CHECK(*reward_shared_ratio <= ratio_boost, "reward_shared_ratio is too large than " + to_string(ratio_boost));
      }

      const auto& core_sym = core_symbol();
      bp_stake_config_singleton stake_config_table(get_self(), get_self().value);
      if (stake_config_table.exists()) {
         check_min_bp_self_stake(producer, stake_config_table.get());
      }

      // Registration commits the current self-stake lifecycle. Unregistering
      // does not clear this bit: that lifecycle remains subject to the
      // configured withdrawal delay until it is fully settled.
      bp_self_stake_table self_stakes(get_self(), get_self().value);
      auto self_stake = self_stakes.find(producer.value);
      if (self_stake != self_stakes.end()) {
         self_stakes.modify(self_stake, same_payer, [&](auto& row) {
            if (!row.generation.has_value()) row.generation = uint64_t{1};
            row.committed = true;
         });
      }
      auto prod = _producers.find( producer.value );
      const auto ct = current_time_point();

      eosio::public_key producer_key{};

      std::visit( [&](auto&& auth ) {
         if( auth.keys.size() == 1 ) {
            // if the producer_authority consists of a single key, use that key in the legacy producer_key field
            producer_key = auth.keys[0].key;
         }
      }, producer_authority );

      if (!flon::voter_reward::is_producer_registered(voter_reward_account, producer)) {
         flon::voter_reward::regproducer_action reg_act{ voter_reward_account, { {producer, active_permission} } };
         reg_act.send( producer );
      }

      if ( prod != _producers.end() ) {
         _producers.modify( prod, producer, [&]( producer_info& info ){
            info.producer_key       = producer_key;
            info.is_active          = true;
            info.url                = url;
            info.location           = location;
            info.producer_authority = producer_authority;
            if ( info.last_claim_time == time_point() )
               info.last_claim_time = ct;
            if (reward_shared_ratio)
               info.reward_shared_ratio = *reward_shared_ratio;
            if (status_url.has_value())
               info.status_url = status_url.value();
         });

      } else {
         _producers.emplace( producer, [&]( producer_info& info ){
            info.owner              = producer;
            info.total_votes        = 0;
            info.producer_key       = producer_key;
            info.is_active          = true;
            info.url                = url;
            info.location           = location;
            info.last_claim_time    = ct;
            info.unclaimed_rewards  = asset(0, core_sym);
            info.producer_authority = producer_authority;
            if (reward_shared_ratio)
               info.reward_shared_ratio = *reward_shared_ratio;
            if (status_url.has_value())
               info.status_url = status_url.value();
         });
      }

   }

   void system_contract::regproducer(  const name& producer, const eosio::public_key& producer_key,
                                       const string& url, uint16_t location,
                                       optional<uint32_t> reward_shared_ratio,
                                       binary_extension<string> status_url ) {
      require_auth( producer );
      check( url.size() < 512, "url too long" );

      register_producer( producer, convert_to_block_signing_authority( producer_key ), url, location,
                         reward_shared_ratio, status_url );
   }

   void system_contract::regproducer2( const name& producer,
                                       const block_signing_authority& producer_authority,
                                       const string& url,
                                       uint16_t location,
                                       optional<uint32_t> reward_shared_ratio,
                                       binary_extension<string> status_url) {
      require_auth( producer );
      check( url.size() < 512, "url too long" );

      std::visit( [&](auto&& auth ) {
         check( auth.is_valid(), "invalid producer authority" );
      }, producer_authority );

      register_producer( producer, producer_authority, url, location, reward_shared_ratio, status_url );
   }

   void system_contract::unregprod( const name& producer ) {
      require_auth( producer );

      const auto& prod = _producers.get( producer.value, "producer not found" );
      _producers.modify( prod, same_payer, [&]( producer_info& info ){
         info.deactivate();
      });
   }

   void system_contract::resumeprod( const name& producer ) {
      require_auth(producer);
      check(_gstate.reward_started_time != time_point(), "producer performance accounting has not started");

      const auto& prod = _producers.get(producer.value, "producer not found");
      check(prod.active(), "producer is not registered as active");

      producer_performance_state_table states(get_self(), get_self().value);
      auto itr = states.find(producer.value);
      check(itr != states.end() &&
            itr->status == static_cast<uint8_t>(bp_performance_status::suspended),
            "producer is not suspended");

      const uint64_t epoch_id = current_reward_epoch(current_time_point());
      check(epoch_id >= itr->suspended_until_epoch, "producer cooldown has not expired");

      states.modify(itr, same_payer, [&](auto& row) {
         row.status = static_cast<uint8_t>(bp_performance_status::probation);
         row.probation_started_epoch = epoch_id;
         row.probation_missed_streak = 0;
      });
   }

   void system_contract::setbpquorum( uint16_t active_bp_quorum ) {
      require_auth(get_self());
      check(active_bp_quorum >= 3 && active_bp_quorum <= 21,
            "active BP quorum must be between 3 and 21");

      election_config_singleton config(get_self(), get_self().value);
      config.set(election_config{active_bp_quorum}, get_self());
   }

   void system_contract::setbpstake(bool enforce_schedule, const asset& min_self_stake,
                                    uint32_t unstake_delay_sec) {
      require_auth(get_self());
      check(min_self_stake.symbol == core_symbol(), "minimum self-stake must use the core symbol");
      check(min_self_stake.amount > 0, "minimum self-stake must be positive");
      #ifdef FLON_TESTNET_SHORT_BP_REFUND
      constexpr uint32_t min_unstake_delay_sec = 30 * 60;
      #else
      constexpr uint32_t min_unstake_delay_sec = 24 * 60 * 60;
      #endif
      check(unstake_delay_sec >= min_unstake_delay_sec,
            "BP unstake delay is below the minimum allowed by this contract build");
      check(unstake_delay_sec <= 365 * 24 * 60 * 60, "BP unstake delay cannot exceed one year");

      bp_stake_config_singleton config(get_self(), get_self().value);
      config.set(bp_stake_config{enforce_schedule, min_self_stake, unstake_delay_sec}, get_self());
   }

   void system_contract::stakebp(const name& owner, const asset& quantity) {
      require_auth(owner);
      check(quantity.symbol == core_symbol(), "BP self-stake must use the core symbol");
      check(quantity.amount > 0, "BP self-stake must be positive");

      bp_stake_refund_table refunds(get_self(), get_self().value);
      check(refunds.find(owner.value) == refunds.end(),
            "pending BP self-stake refund must be claimed or cancelled first");

      eosio::token::transfer_action transfer_act{token_account, {{owner, active_permission}}};
      transfer_act.send(owner, stake_account, quantity, "BP self-stake");

      bp_self_stake_table self_stakes(get_self(), get_self().value);
      auto stake = self_stakes.find(owner.value);
      if (stake == self_stakes.end()) {
         self_stakes.emplace(owner, [&](auto& row) {
            row.owner = owner;
            row.staked = quantity;
            row.generation = uint64_t{1};
            const auto producer = _producers.find(owner.value);
            row.committed = producer != _producers.end() && producer->active();
         });
      } else {
         const bool starts_new_lifecycle = stake->staked.amount == 0;
         const auto producer = _producers.find(owner.value);
         const bool active_producer = producer != _producers.end() && producer->active();
         self_stakes.modify(stake, same_payer, [&](auto& row) {
            const uint64_t current_generation = row.generation.has_value() ? row.generation.value() : uint64_t{1};
            if (starts_new_lifecycle) {
               row.generation = current_generation + 1;
               row.committed = active_producer;
            } else {
               if (!row.generation.has_value()) row.generation = current_generation;
               if (!row.committed.has_value()) row.committed = producer != _producers.end();
            }
            row.staked += quantity;
         });
      }
   }

   void system_contract::unstakebp(const name& owner, const asset& quantity) {
      require_auth(owner);
      check(quantity.symbol == core_symbol(), "BP unstake quantity must use the core symbol");
      check(quantity.amount > 0, "BP unstake quantity must be positive");

      bp_self_stake_table self_stakes(get_self(), get_self().value);
      auto stake = self_stakes.require_find(owner.value, "BP self-stake not found");
      check(stake->staked >= quantity, "insufficient BP self-stake");

      const asset remaining = stake->staked - quantity;
      auto producer = _producers.find(owner.value);
      const bool committed = stake->committed.has_value() ? stake->committed.value() : producer != _producers.end();
      if (producer != _producers.end() && producer->active()) {
         const auto config = get_bp_stake_config(get_self(), core_symbol());
         check(remaining >= config.min_self_stake,
               "registered producer must retain the minimum self-stake");
      }

      if (!committed) {
         self_stakes.modify(stake, same_payer, [&](auto& row) {
            if (!row.generation.has_value()) row.generation = uint64_t{1};
            row.committed = false;
            row.staked = remaining;
         });
         eosio::token::transfer_action transfer_act{token_account, {{stake_account, active_permission}}};
         transfer_act.send(stake_account, owner, quantity, "uncommitted BP self-stake refund");
         return;
      }

      bp_stake_refund_table refunds(get_self(), get_self().value);
      check(refunds.find(owner.value) == refunds.end(), "BP self-stake refund already pending");
      const auto config = get_bp_stake_config(get_self(), core_symbol());
      const uint64_t generation = stake->generation.has_value() ? stake->generation.value() : uint64_t{1};
      refunds.emplace(owner, [&](auto& row) {
         row.owner = owner;
         row.quantity = quantity;
         row.available_at = time_point_sec(current_time_point()) + seconds(config.unstake_delay_sec);
         row.generation = generation;
      });

      self_stakes.modify(stake, same_payer, [&](auto& row) {
         row.staked = remaining;
         row.generation = generation;
         row.committed = true;
      });
   }

   void system_contract::claimbpstake(const name& owner) {
      require_auth(owner);
      bp_stake_refund_table refunds(get_self(), get_self().value);
      auto refund = refunds.require_find(owner.value, "BP self-stake refund not found");
      check(refund->available_at <= time_point_sec(current_time_point()),
            "BP self-stake refund is not available yet");

      bp_self_stake_table self_stakes(get_self(), get_self().value);
      auto stake = self_stakes.find(owner.value);
      if (stake != self_stakes.end() && refund->generation.has_value()) {
         check((stake->generation.has_value() ? stake->generation.value() : uint64_t{1}) == refund->generation.value(),
               "BP self-stake refund generation mismatch");
      }

      const asset quantity = refund->quantity;
      refunds.erase(refund);
      eosio::token::transfer_action transfer_act{token_account, {{stake_account, active_permission}}};
      transfer_act.send(stake_account, owner, quantity, "BP self-stake refund");
   }

   void system_contract::cancelbpref(const name& owner) {
      require_auth(owner);
      bp_stake_refund_table refunds(get_self(), get_self().value);
      auto refund = refunds.require_find(owner.value, "BP self-stake refund not found");
      const asset quantity = refund->quantity;
      const uint64_t generation = refund->generation.has_value() ? refund->generation.value() : uint64_t{1};

      bp_self_stake_table self_stakes(get_self(), get_self().value);
      auto stake = self_stakes.find(owner.value);
      if (stake == self_stakes.end()) {
         self_stakes.emplace(owner, [&](auto& row) {
            row.owner = owner;
            row.staked = quantity;
            row.generation = generation;
            row.committed = true;
         });
      } else {
         check((stake->generation.has_value() ? stake->generation.value() : generation) == generation,
               "BP self-stake refund generation mismatch");
         self_stakes.modify(stake, same_payer, [&](auto& row) {
            row.staked += quantity;
            row.generation = generation;
            row.committed = true;
         });
      }
      refunds.erase(refund);
   }

   void system_contract::update_elected_producers( const block_timestamp& block_time ) {
      _gstate.last_producer_schedule_update = block_time;

      auto idx = _producers.get_index<"prototalvote"_n>();

      using value_type = std::pair<eosio::producer_authority, uint16_t>;
      std::vector< value_type > top_producers;
      std::vector< finalizer_auth_info > proposed_finalizers;
      election_config_singleton election_config_tbl(get_self(), get_self().value);
      const uint16_t active_bp_quorum = election_config_tbl.exists()
         ? election_config_tbl.get().active_bp_quorum : 21;
      const auto stake_config = get_bp_stake_config(get_self(), core_symbol());
      top_producers.reserve(active_bp_quorum);
      proposed_finalizers.reserve(active_bp_quorum);

      bool is_savanna = is_savanna_consensus();
      const bool performance_started = _gstate.reward_started_time != time_point() &&
                                       current_time_point() >= _gstate.reward_started_time;
      const uint64_t performance_epoch = performance_started
                                           ? current_reward_epoch(current_time_point()) : 0;

      uint32_t candidates_scanned = 0;
      for( auto it = idx.cbegin(); it != idx.cend() &&
           top_producers.size() < active_bp_quorum && 0 < it->total_votes &&
           candidates_scanned < max_schedule_candidates_scanned;
           ++it, ++candidates_scanned ) {
         if (!it->active()) continue;
         if (stake_config.enforce_schedule && !has_min_bp_self_stake(it->owner, stake_config)) continue;
         if (performance_started && !is_performance_eligible(it->owner, performance_epoch)) continue;
         if( is_savanna ) {
            auto finalizer = _finalizers.find( it->owner.value );
            if( finalizer == _finalizers.end() ) {
               // The producer is not in finalizers table, indicating it does not have an
               // active registered finalizer key. Try next one.
               continue;
            }

            // This should never happen. Double check just in case
            if( finalizer->active_key_binary.empty() ) {
               continue;
            }

            proposed_finalizers.emplace_back(*finalizer);
         }

         top_producers.emplace_back(
            eosio::producer_authority{
               .producer_name = it->owner,
               .authority     = it->producer_authority
            },
            it->location
         );
      }

      // A suspension must be able to remove a producer even when the eligible
      // replacement set is smaller than the previous schedule. Keeping the old
      // schedule would leave the suspended producer assigned to new slots.
      if( top_producers.empty() ) {
         return;
      }

      std::sort( top_producers.begin(), top_producers.end(), []( const value_type& lhs, const value_type& rhs ) {
         return lhs.first.producer_name < rhs.first.producer_name; // sort by producer name
         // return lhs.second < rhs.second; // sort by location
      } );

      std::vector<eosio::producer_authority> producers;

      producers.reserve(top_producers.size());
      for( auto& item : top_producers )
         producers.push_back( std::move(item.first) );

      if( set_proposed_producers( producers ) >= 0 ) {
         _gstate.last_producer_schedule_size = static_cast<decltype(_gstate.last_producer_schedule_size)>( producers.size() );
      }

      // set_proposed_finalizers() checks if last proposed finalizer policy
      // has not changed, it will not call set_finalizers() host function.
      if( is_savanna ) {
         set_proposed_finalizers( std::move(proposed_finalizers) );
      }
   }

   // double stake2vote( int64_t staked ) {
   //    /// TODO subtract 2080 brings the large numbers closer to this decade
   //    double weight = int64_t( (current_time_point().sec_since_epoch() - (block_timestamp::block_timestamp_epoch / 1000)) / (seconds_per_day * 7) )  / double( 52 );
   //    return double(staked) * std::pow( 2, weight );
   // }

   std::vector<vote_allocation> system_contract::equal_vote_allocations(
         const std::vector<name>& producers, int64_t votes) const {
      std::vector<vote_allocation> result;
      if (producers.empty()) return result;
      const int64_t weight = votes / static_cast<int64_t>(producers.size());
      result.reserve(producers.size());
      for (const auto& producer : producers) result.push_back({producer, weight});
      return result;
   }

   std::vector<vote_allocation> system_contract::target_vote_allocations(
         const name& voter, const std::vector<name>& producers, int64_t votes,
         bool pending) const {
      if (pending) {
         pending_voter_allocations_table table(get_self(), get_self().value);
         auto itr = table.find(voter.value);
         if (itr != table.end()) return itr->allocations;
      } else {
         voter_allocations_table table(get_self(), get_self().value);
         auto itr = table.find(voter.value);
         if (itr != table.end()) return itr->allocations;
      }
      return equal_vote_allocations(producers, votes);
   }

   void system_contract::update_pending_allocation_deltas(
         const std::vector<vote_allocation>& old_allocations,
         const std::vector<vote_allocation>& new_allocations) {
      pending_vote_deltas_table deltas(get_self(), get_self().value);
      auto old_itr = old_allocations.begin();
      auto new_itr = new_allocations.begin();
      while (old_itr != old_allocations.end() || new_itr != new_allocations.end()) {
         name producer;
         int64_t delta = 0;
         if (new_itr == new_allocations.end() ||
             (old_itr != old_allocations.end() && old_itr->producer < new_itr->producer)) {
            producer = old_itr->producer;
            delta = -old_itr->votes;
            ++old_itr;
         } else if (old_itr == old_allocations.end() || new_itr->producer < old_itr->producer) {
            producer = new_itr->producer;
            delta = new_itr->votes;
            ++new_itr;
         } else {
            producer = old_itr->producer;
            delta = new_itr->votes - old_itr->votes;
            ++old_itr;
            ++new_itr;
         }
         auto delta_itr = deltas.find(producer.value);
         if (delta_itr == deltas.end()) {
            deltas.emplace(get_self(), [&](auto& row) {
               row.producer = producer;
               row.votes_delta = delta;
            });
         } else {
            deltas.modify(delta_itr, same_payer, [&](auto& row) { row.votes_delta += delta; });
         }
      }
   }

   void system_contract::voteproducer( const name& voter_name, const std::vector<name>& producers ) {

      require_auth(voter_name);

      check( producers.size() <= max_vote_producer_count, "attempt to vote for too many producers" );
      for( size_t i = 1; i < producers.size(); ++i ) {
         check( producers[i - 1] < producers[i], "producer votes must be unique and sorted" );
      }

      materialize_pending_vote(voter_name);
      auto voter_itr = _voters.find( voter_name.value );
      check( voter_itr != _voters.end(), "voter not found" ); /// addvote creates voter object

      ASSERT( voter_itr->votes >= 0 )
      pending_voters_table pending_votes(get_self(), get_self().value);
      auto pending_itr = pending_votes.find(voter_name.value);
      const int64_t target_votes = pending_itr == pending_votes.end()
                                     ? voter_itr->votes : pending_itr->votes;
      const auto& current_target = pending_itr == pending_votes.end()
                                     ? voter_itr->producers : pending_itr->producers;
      CHECKC( current_target != producers, err::VOTE_CHANGE_ERROR, "producers no change" )

      auto now = current_time_point();
      // CHECK( time_point(voter_itr->last_unvoted_time) + seconds(vote_interval_sec) < now, "Voter can only vote or subvote once a day" )

      const uint64_t activation_epoch = next_vote_epoch();
      queue_vote_change(voter_name, target_votes, producers, activation_epoch);

      flon::voter_reward::voteproducer_action voteproducer_act{ voter_reward_account, { {get_self(), active_permission}, {voter_name, active_permission} } };
      voteproducer_act.send( voter_name, producers, activation_epoch );

      _voters.modify( voter_itr, same_payer, [&]( auto& v ) { v.last_unvoted_time = now; });
   }

   void system_contract::votealloc(const name& voter_name,
                                   const std::vector<vote_allocation>& allocations) {
      require_auth(voter_name);
      check(allocations.size() <= max_vote_producer_count,
            "attempt to vote for too many producers");

      int64_t allocated = 0;
      std::vector<name> producers;
      producers.reserve(allocations.size());
      for (size_t i = 0; i < allocations.size(); ++i) {
         const auto& allocation = allocations[i];
         check(allocation.votes > 0, "producer vote allocation must be positive");
         if (i > 0) check(allocations[i - 1].producer < allocation.producer,
                          "producer vote allocations must be unique and sorted");
         check(allocated <= std::numeric_limits<int64_t>::max() - allocation.votes,
               "producer vote allocation overflow");
         allocated += allocation.votes;
         producers.push_back(allocation.producer);
         auto producer_itr = _producers.find(allocation.producer.value);
         check(producer_itr != _producers.end(),
               "producer " + allocation.producer.to_string() + " is not registered");
         check(producer_itr->active(),
               "producer " + allocation.producer.to_string() + " is not active");
      }

      materialize_pending_vote(voter_name);
      auto voter_itr = _voters.find(voter_name.value);
      check(voter_itr != _voters.end(), "voter not found");
      pending_voters_table pending_votes(get_self(), get_self().value);
      auto pending_itr = pending_votes.find(voter_name.value);
      const bool has_pending = pending_itr != pending_votes.end();
      const int64_t target_votes = has_pending ? pending_itr->votes : voter_itr->votes;
      check(allocated == target_votes,
            "sum of producer vote allocations must equal voter locked FLON");

      const auto old_allocations = target_vote_allocations(
         voter_name, has_pending ? pending_itr->producers : voter_itr->producers,
         target_votes, has_pending);
      check(old_allocations != allocations, "producer vote allocations did not change");
      const uint64_t activation_epoch = next_vote_epoch();
      update_pending_allocation_deltas(old_allocations, allocations);

      if (pending_itr == pending_votes.end()) {
         pending_votes.emplace(voter_name, [&](auto& row) {
            row.voter = voter_name;
            row.votes = target_votes;
            row.producers = producers;
            row.activation_epoch = activation_epoch;
         });
      } else {
         pending_votes.modify(pending_itr, voter_name, [&](auto& row) {
            row.producers = producers;
            row.activation_epoch = activation_epoch;
         });
      }
      pending_voter_allocations_table pending_allocations(get_self(), get_self().value);
      auto allocation_itr = pending_allocations.find(voter_name.value);
      if (allocation_itr == pending_allocations.end()) {
         pending_allocations.emplace(voter_name, [&](auto& row) {
            row.voter = voter_name;
            row.allocations = allocations;
            row.activation_epoch = activation_epoch;
         });
      } else {
         pending_allocations.modify(allocation_itr, voter_name, [&](auto& row) {
            row.allocations = allocations;
            row.activation_epoch = activation_epoch;
         });
      }

      flon::voter_reward::votealloc_action votealloc_act{
         voter_reward_account, {{get_self(), active_permission}, {voter_name, active_permission}}
      };
      std::vector<flon::voter_reward::vote_allocation> reward_allocations;
      reward_allocations.reserve(allocations.size());
      for (const auto& allocation : allocations) {
         reward_allocations.push_back({allocation.producer, allocation.votes});
      }
      votealloc_act.send(voter_name, reward_allocations, activation_epoch);
      _voters.modify(voter_itr, same_payer, [&](auto& row) {
         row.last_unvoted_time = current_time_point();
      });
   }

   void system_contract::update_vote_allocations( const std::vector<name>& old_producers, int64_t old_votes,
                                                   const std::vector<name>& new_producers, int64_t new_votes ) {
      ASSERT( old_votes >= 0 && new_votes >= 0 )

      // One locked-token unit creates at most one aggregate unit of ranking power.
      // Integer division intentionally leaves the remainder unallocated.
      const int64_t old_weight = old_producers.empty() ? 0 : old_votes / static_cast<int64_t>(old_producers.size());
      const int64_t new_weight = new_producers.empty() ? 0 : new_votes / static_cast<int64_t>(new_producers.size());

      auto old_itr = old_producers.begin();
      auto new_itr = new_producers.begin();
      while( old_itr != old_producers.end() || new_itr != new_producers.end() ) {
         if( new_itr == new_producers.end() ||
             (old_itr != old_producers.end() && *old_itr < *new_itr) ) {
            update_producer_votes( {*old_itr}, -old_weight );
            ++old_itr;
         } else if( old_itr == old_producers.end() || *new_itr < *old_itr ) {
            update_producer_votes( {*new_itr}, new_weight );
            ++new_itr;
         } else {
            update_producer_votes( {*old_itr}, new_weight - old_weight );
            ++old_itr;
            ++new_itr;
         }
      }
   }

   void system_contract::update_producer_votes( const std::vector<name>& producers,
                                                int64_t votes_delta ) {
      for( const auto& p : producers ) {
         auto pitr = _producers.find( p.value );

         CHECK( pitr != _producers.end(), "producer " + p.to_string() + " is not registered" );

         if (votes_delta > 0) {
            CHECK( pitr->active() , "producer " + pitr->owner.to_string() + " is not active" );
         }
         // CHECK(pitr->ext, "producer " + pitr->owner.to_string() + " is not updated by regproducer")

         _producers.modify( pitr, same_payer, [&]( auto& p ) {
            p.total_votes += votes_delta;
            CHECK( p.total_votes >= 0, "producer's elected votes can not be negative" )
            // _elect_gstate.total_producer_elected_votes += votes_delta;
            // CHECK(_elect_gstate.total_producer_elected_votes >= 0, "total_producer_elected_votes can not be negative");
         });
      }
   }

   void system_contract::materialize_pending_vote( const name& voter ) {
      pending_voters_table pending_votes(get_self(), get_self().value);
      auto pending_itr = pending_votes.find(voter.value);
      if (pending_itr == pending_votes.end()) return;

      reward_epoch_singleton epoch_state_tbl(get_self(), get_self().value);
      auto epoch_state = epoch_state_tbl.exists() ? epoch_state_tbl.get() : reward_epoch_state{};
      if (pending_itr->activation_epoch > epoch_state.next_epoch_to_close) return;

      auto voter_itr = _voters.find(voter.value);
      CHECK(voter_itr != _voters.end(), "voter not found")
      _voters.modify(voter_itr, same_payer, [&](auto& row) {
         row.votes = pending_itr->votes;
         row.producers = pending_itr->producers;
      });
      pending_voter_allocations_table pending_allocations(get_self(), get_self().value);
      voter_allocations_table active_allocations(get_self(), get_self().value);
      auto pending_allocation_itr = pending_allocations.find(voter.value);
      auto active_allocation_itr = active_allocations.find(voter.value);
      if (pending_allocation_itr != pending_allocations.end()) {
         if (active_allocation_itr == active_allocations.end()) {
            active_allocations.emplace(voter, [&](auto& row) {
               row.voter = voter;
               row.allocations = pending_allocation_itr->allocations;
            });
         } else {
            active_allocations.modify(active_allocation_itr, same_payer, [&](auto& row) {
               row.allocations = pending_allocation_itr->allocations;
            });
         }
         pending_allocations.erase(pending_allocation_itr);
      } else if (active_allocation_itr != active_allocations.end()) {
         active_allocations.erase(active_allocation_itr);
      }
      pending_votes.erase(pending_itr);
   }

   void system_contract::update_pending_vote_deltas(const std::vector<name>& old_producers,
                                                     int64_t old_votes,
                                                     const std::vector<name>& new_producers,
                                                     int64_t new_votes) {
      const int64_t old_weight = old_producers.empty()
                                   ? 0 : old_votes / static_cast<int64_t>(old_producers.size());
      const int64_t new_weight = new_producers.empty()
                                   ? 0 : new_votes / static_cast<int64_t>(new_producers.size());
      pending_vote_deltas_table deltas(get_self(), get_self().value);

      auto old_itr = old_producers.begin();
      auto new_itr = new_producers.begin();
      while (old_itr != old_producers.end() || new_itr != new_producers.end()) {
         name producer;
         int64_t delta = 0;
         if (new_itr == new_producers.end() ||
             (old_itr != old_producers.end() && *old_itr < *new_itr)) {
            producer = *old_itr++;
            delta = -old_weight;
         } else if (old_itr == old_producers.end() || *new_itr < *old_itr) {
            producer = *new_itr++;
            delta = new_weight;
         } else {
            producer = *old_itr++;
            ++new_itr;
            delta = new_weight - old_weight;
         }

         auto delta_itr = deltas.find(producer.value);
         if (delta_itr == deltas.end()) {
            deltas.emplace(get_self(), [&](auto& row) {
               row.producer = producer;
               row.votes_delta = delta;
            });
         } else {
            deltas.modify(delta_itr, same_payer, [&](auto& row) { row.votes_delta += delta; });
         }
      }
   }

   void system_contract::queue_vote_change(const name& voter, int64_t votes,
                                            const std::vector<name>& producers,
                                            uint64_t activation_epoch) {
      CHECK(votes >= 0, "voter's votes can not be negative")
      pending_voters_table pending_votes(get_self(), get_self().value);
      auto pending_itr = pending_votes.find(voter.value);
      auto voter_itr = _voters.find(voter.value);
      CHECK(voter_itr != _voters.end(), "voter not found")

      if (activation_epoch == 0) {
         update_vote_allocations(voter_itr->producers, voter_itr->votes, producers, votes);
         _voters.modify(voter_itr, same_payer, [&](auto& row) {
            row.votes = votes;
            row.producers = producers;
         });
         return;
      }

      std::vector<name> old_producers;
      int64_t old_votes = 0;
      bool had_pending = pending_itr != pending_votes.end();
      if (pending_itr == pending_votes.end()) {
         old_producers = voter_itr->producers;
         old_votes = voter_itr->votes;
      } else {
         old_producers = pending_itr->producers;
         old_votes = pending_itr->votes;
      }
      const auto old_allocations = target_vote_allocations(voter, old_producers, old_votes,
                                                            had_pending);
      const auto new_allocations = equal_vote_allocations(producers, votes);
      update_pending_allocation_deltas(old_allocations, new_allocations);

      pending_voter_allocations_table pending_allocations(get_self(), get_self().value);
      auto pending_allocation_itr = pending_allocations.find(voter.value);
      if (pending_allocation_itr != pending_allocations.end()) {
         pending_allocations.erase(pending_allocation_itr);
      }

      if (pending_itr == pending_votes.end()) {
         pending_votes.emplace(voter, [&](auto& row) {
            row.voter = voter;
            row.votes = votes;
            row.producers = producers;
            row.activation_epoch = activation_epoch;
         });
      } else {
         pending_votes.modify(pending_itr, voter, [&](auto& row) {
            row.votes = votes;
            row.producers = producers;
            row.activation_epoch = activation_epoch;
         });
      }
   }

   void system_contract::addvote( const name& voter, const asset& vote_staked ) {
      require_auth(voter);

      CHECK(vote_staked.symbol == core_symbol(), "vote_staked must be core symbol")
      CHECK(vote_staked.amount > 0, "vote_staked must be positive")

      if (_gstate.total_vote_stake.amount == 0) {
         _gstate.total_vote_stake = vote_staked;
      } else {
         _gstate.total_vote_stake += vote_staked;
      }

      auto votes = vote_staked.amount;
      eosio::token::transfer_action transfer_act{ token_account, { {voter, active_permission} } };
      transfer_act.send( voter, vote_account, vote_staked, "addvote" );

      materialize_pending_vote(voter);
      auto voter_itr = _voters.find( voter.value );
      if( voter_itr != _voters.end() ) {
      } else {
         _voters.emplace( voter, [&]( auto& v ) {
            v.owner              = voter;
            v.votes              = 0;
         });
         voter_itr = _voters.find(voter.value);
      }

      pending_voters_table pending_votes(get_self(), get_self().value);
      auto pending_itr = pending_votes.find(voter.value);
      const int64_t target_votes = (pending_itr == pending_votes.end()
                                      ? voter_itr->votes : pending_itr->votes) + votes;
      const auto target_producers = pending_itr == pending_votes.end()
                                      ? voter_itr->producers : pending_itr->producers;
      const uint64_t activation_epoch = next_vote_epoch();
      queue_vote_change(voter, target_votes, target_producers, activation_epoch);

      // Governance power is an internal DAO ledger derived directly from the
      // voter's target stake. It is not represented by a transferable token.
      if (eosio::is_account("dao.gov"_n)) {
         const asset governance_power(
            target_votes, symbol(symbol_code("POWER"), vote_staked.symbol.precision()));
         eosio::action({get_self(), active_permission}, "dao.gov"_n, "setpower"_n,
                       std::make_tuple(voter, governance_power)).send();
      }

      flon::voter_reward::addvote_action addvote_act{ voter_reward_account, { {get_self(), active_permission}, {voter, active_permission} } };
      addvote_act.send( voter, votes, activation_epoch );
   }

   void system_contract::subvote( const name& voter, const asset& vote_staked ) {
      require_auth(voter);
      auto now = current_time_point();
      CHECK( _gstate.election_activated_time != time_point() && _gstate.election_activated_time <= now,
             "cannot subvote until the election is activated" );

      CHECK(vote_staked.symbol == core_symbol(), "vote_staked must be core symbol")
      CHECK(vote_staked.amount > 0, "vote_staked must be positive")

      auto votes = vote_staked.amount;
      materialize_pending_vote(voter);
      auto voter_itr = _voters.find( voter.value );
      CHECK( voter_itr != _voters.end(), "voter not found" )

      pending_voters_table pending_votes(get_self(), get_self().value);
      auto pending_itr = pending_votes.find(voter.value);
      const int64_t current_target_votes = pending_itr == pending_votes.end()
                                             ? voter_itr->votes : pending_itr->votes;
      const auto target_producers = pending_itr == pending_votes.end()
                                      ? voter_itr->producers : pending_itr->producers;
      CHECK( current_target_votes >= votes, "votes insufficent" )

      CHECK(_gstate.total_vote_stake >= vote_staked, "Total vote stake insufficent")
      _gstate.total_vote_stake -= vote_staked;

      // CHECKC( time_point(voter_itr->last_unvoted_time) + seconds(vote_interval_sec) < now, err::VOTE_ERROR, "Voter can only vote or subvote once a day" )

      vote_refund_table vote_refund_tbl( get_self(), voter.value );
      CHECKC( vote_refund_tbl.find( voter.value ) == vote_refund_tbl.end(), err::VOTE_REFUND_ERROR, "This account already has a vote refund" );

      const uint64_t activation_epoch = next_vote_epoch();
      const int64_t target_votes = current_target_votes - votes;
      queue_vote_change(voter, target_votes, target_producers, activation_epoch);

      if (eosio::is_account("dao.gov"_n)) {
         const asset governance_power(
            target_votes, symbol(symbol_code("POWER"), vote_staked.symbol.precision()));
         eosio::action({get_self(), active_permission}, "dao.gov"_n, "setpower"_n,
                       std::make_tuple(voter, governance_power)).send();
      }

      _voters.modify( voter_itr, same_payer, [&]( auto& v ) {
         v.last_unvoted_time  = now;
      });

      flon::voter_reward::subvote_action subvote_act{ voter_reward_account, { {get_self(), active_permission}, {voter, active_permission} } };
      subvote_act.send( voter, votes, activation_epoch );

      vote_refund_tbl.emplace( voter, [&]( auto& r ) {
         r.owner = voter;
         r.vote_staked = vote_staked;
         r.request_time = now;
      });

   }

   void system_contract::voterefund( const name& owner ) {
      vote_refund_table vote_refund_tbl( get_self(), owner.value );
      auto itr = vote_refund_tbl.find( owner.value );
      check( itr != vote_refund_tbl.end(), "no vote refund found" );
      check( itr->request_time + seconds(refund_delay_sec) <= current_time_point(), "refund period not mature yet" );

      eosio::token::transfer_action transfer_act{ token_account, { {vote_account, active_permission} } };
      transfer_act.send( vote_account, itr->owner, itr->vote_staked, "voterefund" );
      vote_refund_tbl.erase( itr );
   }

   #else

   void system_contract::setprods( const std::vector<eosio::producer_authority>& schedule ) {
      require_auth( get_self() );
      set_proposed_producers( schedule );
   }

   #endif//ENABLE_VOTING_PRODUCER
} /// namespace eosiosystem

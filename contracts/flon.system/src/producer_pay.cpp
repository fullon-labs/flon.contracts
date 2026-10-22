#include <flon.system/flon.system.hpp>
#include <flon.token/flon.token.hpp>
#include <voter.reward/voter.reward.hpp>

#include <eosio/crypto.hpp>
#include <eosio/serialize.hpp>

#include <algorithm>
#include <limits>

namespace eosiosystem {

   using eosio::current_time_point;
   using eosio::microseconds;
   using eosio::token;


   int64_t mining_reward_cap_amount(const symbol& core) {
      constexpr int64_t mining_reward_cap_tokens = 500000000;
      int64_t scale = 1;
      for (uint8_t i = 0; i < core.precision(); ++i) {
         check(scale <= std::numeric_limits<int64_t>::max() / 10,
               "core token precision is too large for the mining cap");
         scale *= 10;
      }
      check(scale <= std::numeric_limits<int64_t>::max() / mining_reward_cap_tokens,
            "mining cap exceeds asset amount range");
      return mining_reward_cap_tokens * scale;
   }

   int64_t scheduled_reward_for_block(uint64_t rewarded_block_count,
                                      int64_t mining_cap_amount) {
      const uint64_t era = rewarded_block_count / reward_halving_period_blocks;
      // Once the integer era budget reaches zero, no further smallest units can
      // be issued. Avoid an undefined shift for very large era numbers.
      if (era >= 63)
         return 0;

      const int64_t era_budget = mining_cap_amount / (int64_t{1} << (era + 1));
      if (era_budget <= 0)
         return 0;

      const uint64_t offset = rewarded_block_count % reward_halving_period_blocks;
      using uint128_t = unsigned __int128;
      const auto cumulative_after = static_cast<int64_t>(
         (uint128_t(offset + 1) * static_cast<uint64_t>(era_budget)) /
         reward_halving_period_blocks);
      const auto cumulative_before = static_cast<int64_t>(
         (uint128_t(offset) * static_cast<uint64_t>(era_budget)) /
         reward_halving_period_blocks);
      return cumulative_after - cumulative_before;
   }

   uint64_t rewarded_block_count_from_issued(int64_t issued_amount,
                                             int64_t mining_cap_amount) {
      if (issued_amount <= 0)
         return 0;

      uint64_t count = 0;
      int64_t remaining_issued = std::min(issued_amount, mining_cap_amount);
      using uint128_t = unsigned __int128;
      for (uint64_t era = 0; era < 63; ++era) {
         const int64_t era_budget = mining_cap_amount / (int64_t{1} << (era + 1));
         if (era_budget <= 0)
            break;
         if (remaining_issued >= era_budget) {
            remaining_issued -= era_budget;
            count += reward_halving_period_blocks;
            continue;
         }

         // Round up so a legacy amount that did not land exactly on this
         // schedule can never restart from an earlier, higher-paying block.
         const auto numerator = uint128_t(static_cast<uint64_t>(remaining_issued)) *
                                reward_halving_period_blocks +
                                static_cast<uint64_t>(era_budget) - 1;
         count += static_cast<uint64_t>(numerator / static_cast<uint64_t>(era_budget));
         return count;
      }
      return count;
   }

   uint64_t system_contract::current_reward_epoch( const time_point& now ) const {
      check(_gstate.reward_started_time != time_point(), "producer rewards have not started");
      check(now >= _gstate.reward_started_time, "producer rewards have not started");
      const block_timestamp now_slot(now);
      const block_timestamp start_slot(_gstate.reward_started_time);
      return (now_slot.slot - start_slot.slot) / reward_epoch_slots;
   }

   uint64_t system_contract::reward_epoch_for_slot( uint32_t slot ) const {
      check(_gstate.reward_started_time != time_point(), "producer rewards have not started");
      const block_timestamp start_slot(_gstate.reward_started_time);
      check(slot >= start_slot.slot, "slot is before producer rewards started");
      return (slot - start_slot.slot) / reward_epoch_slots;
   }

   void system_contract::accumulate_producer_slot_stats( uint64_t epoch_id,
                                                          uint32_t from_slot,
                                                          uint32_t to_slot ) {
      const auto stats = eosio::get_producer_slot_stats(from_slot, to_slot);
      bp_performance_table bp_stats(get_self(), epoch_id);

      for (const auto& sample : stats) {
         auto itr = bp_stats.find(sample.producer.value);
         if (itr == bp_stats.end()) {
            bp_stats.emplace(get_self(), [&](auto& row) {
               row.producer = sample.producer;
               row.epoch_id = epoch_id;
               row.expected_blocks = sample.expected;
               row.produced_blocks = sample.produced;
               row.missed_blocks = sample.missed;
               row.longest_missed_streak = sample.longest_missed_streak;
               row.trailing_missed_streak = sample.trailing_missed_streak;
            });
         } else {
            bp_stats.modify(itr, same_payer, [&](auto& row) {
               const uint64_t joined_streak = row.trailing_missed_streak +
                                              sample.leading_missed_streak;
               row.expected_blocks += sample.expected;
               row.produced_blocks += sample.produced;
               row.missed_blocks += sample.missed;
               row.longest_missed_streak = std::max(
                  row.longest_missed_streak,
                  std::max<uint64_t>(sample.longest_missed_streak, joined_streak));
               // Preserve a miss streak across intrinsic query/onblock boundaries.
               // If this entire sample contains only missed assignments, its
               // leading and trailing streak are the same continuing streak.
               row.trailing_missed_streak = sample.expected > 0 &&
                                             sample.produced == 0 &&
                                             sample.missed == sample.expected
                                                ? joined_streak
                                                : sample.trailing_missed_streak;
            });
         }

         // Keep the probation fuse continuous across epoch and intrinsic-query
         // boundaries. bpstats is epoch scoped, so its trailing streak alone
         // cannot detect a streak that starts in the previous epoch.
         if (sample.expected > 0) {
            producer_performance_state_table states(get_self(), get_self().value);
            auto state_itr = states.find(sample.producer.value);
            if (state_itr != states.end() &&
                state_itr->status == static_cast<uint8_t>(bp_performance_status::probation)) {
               const uint32_t prior_streak = state_itr->probation_missed_streak.has_value()
                                                ? state_itr->probation_missed_streak.value() : 0;
               const bool all_assigned_slots_missed = sample.produced == 0 &&
                                                       sample.missed == sample.expected;
               const uint64_t updated_streak = all_assigned_slots_missed
                  ? static_cast<uint64_t>(prior_streak) + sample.missed
                  : sample.trailing_missed_streak;
               const bool suspend = updated_streak >= probation_missed_streak_limit;
               const uint32_t suspension_count = state_itr->suspension_count + (suspend ? 1 : 0);
               const uint32_t warning_count = state_itr->warning_count + (suspend ? 1 : 0);
               const uint64_t suspended_until = suspend
                  ? epoch_id + suspension_cooldown_epochs(suspension_count) + 1
                  : state_itr->suspended_until_epoch;
               states.modify(state_itr, same_payer, [&](auto& row) {
                  row.probation_missed_streak = static_cast<uint32_t>(
                     std::min<uint64_t>(updated_streak, std::numeric_limits<uint32_t>::max()));
                  if (suspend) {
                     row.status = static_cast<uint8_t>(bp_performance_status::suspended);
                     row.warning_count = warning_count;
                     row.suspension_count = suspension_count;
                     row.suspended_until_epoch = suspended_until;
                     row.probation_started_epoch = 0;
                     row.probation_missed_streak = 0;
                  }
               });
               if (suspend) {
                  auto stats_itr = bp_stats.find(sample.producer.value);
                  if (stats_itr != bp_stats.end()) {
                     bp_stats.modify(stats_itr, same_payer, [&](auto& row) {
                        row.status = static_cast<uint8_t>(bp_performance_status::suspended);
                        row.warning_count = warning_count;
                     });
                  }
               }
            }
         }
      }

      epoch_performance_table epochs(get_self(), get_self().value);
      auto epoch_itr = epochs.find(epoch_id);
      if (epoch_itr == epochs.end()) {
         const block_timestamp reward_start(_gstate.reward_started_time);
         const uint64_t epoch_start = static_cast<uint64_t>(reward_start.slot) +
                                      epoch_id * reward_epoch_slots;
         const auto active_producers = eosio::get_active_producers();
         const auto packed_schedule = eosio::pack(active_producers);
         const auto schedule_hash = eosio::sha256(packed_schedule.data(), packed_schedule.size());
         epochs.emplace(get_self(), [&](auto& row) {
            row.epoch_id = epoch_id;
            row.start_slot = static_cast<uint32_t>(epoch_start);
            row.end_slot = static_cast<uint32_t>(epoch_start + reward_epoch_slots - 1);
            row.schedule_hash = schedule_hash;
            row.complete_sample = from_slot == epoch_start;
         });
      }
   }

   void system_contract::record_producer_slot_stats( const block_timestamp& timestamp ) {
      performance_accounting_singleton accounting(get_self(), get_self().value);
      auto state = accounting.exists() ? accounting.get() : performance_accounting_state{};

      static constexpr uint8_t performance_accounting_version = 1;
      const bool current_version = state.version.has_value() &&
                                   state.version.value() == performance_accounting_version;

      // A contract upgrade must not trust a cursor written by an older
      // accounting implementation. Bootstrap with the pending block, then use
      // only the cursor committed by the preceding successful onblock.
      uint64_t from_slot = current_version && state.last_accounted_slot < timestamp.slot
                              ? static_cast<uint64_t>(state.last_accounted_slot) + 1
                              : timestamp.slot;

      // The intrinsic can only inspect the gap between the current head and
      // this pending block. Do not attempt an unbounded catch-up inside
      // onblock: after an exceptionally long outage, skip the oversized gap
      // and let the affected epochs remain incomplete and therefore exempt
      // from individual penalties.
      const uint64_t pending_slot_count = static_cast<uint64_t>(timestamp.slot) - from_slot + 1;
      if (pending_slot_count > producer_stats_onblock_limit) {
         state.last_accounted_slot = timestamp.slot;
         state.version = performance_accounting_version;
         accounting.set(state, get_self());
         return;
      }

      const block_timestamp reward_start(_gstate.reward_started_time);
      while (from_slot <= timestamp.slot) {
         const uint64_t epoch_id = reward_epoch_for_slot(static_cast<uint32_t>(from_slot));
         const uint64_t epoch_end = static_cast<uint64_t>(reward_start.slot) +
                                    (epoch_id + 1) * reward_epoch_slots - 1;
         const uint64_t query_end = std::min<uint64_t>(timestamp.slot, epoch_end);
         accumulate_producer_slot_stats(epoch_id, static_cast<uint32_t>(from_slot),
                                        static_cast<uint32_t>(query_end));
         from_slot = query_end + 1;
      }

      state.last_accounted_slot = timestamp.slot;
      state.version = performance_accounting_version;
      accounting.set(state, get_self());
   }

   bool system_contract::is_performance_eligible( const name& producer, uint64_t epoch_id ) const {
      (void)epoch_id;
      producer_performance_state_table states(get_self(), get_self().value);
      const auto itr = states.find(producer.value);
      return itr == states.end() ||
             itr->status != static_cast<uint8_t>(bp_performance_status::suspended);
   }

   uint64_t system_contract::suspension_cooldown_epochs( uint32_t suspension_count ) const {
      if (suspension_count == 0) return 1;
      const uint32_t shift = std::min<uint32_t>(suspension_count - 1, 3);
      return std::min<uint64_t>(uint64_t{1} << shift, max_suspension_epochs);
   }

   uint32_t system_contract::reward_factor_basis_points( const bp_performance* stats,
                                                          bool network_incident ) const {
      if (stats == nullptr || network_incident || stats->expected_blocks < min_expected_blocks)
         return ratio_boost;
      if (stats->status == static_cast<uint8_t>(bp_performance_status::insufficient_sample) ||
          stats->status == static_cast<uint8_t>(bp_performance_status::network_incident))
         return ratio_boost;
      if (stats->produced_blocks * 100 < stats->expected_blocks * 95)
         return 0;
      if (stats->produced_blocks * 100 >= stats->expected_blocks * 99)
         return ratio_boost;
      return static_cast<uint32_t>(
         static_cast<unsigned __int128>(stats->produced_blocks) * ratio_boost /
         stats->expected_blocks);
   }

   void system_contract::onblock( ignore<block_header> ) {
      using namespace eosio;

      require_auth(get_self());

      // Deserialize needed fields from block header.
      block_timestamp timestamp;
      name            producer;
      uint16_t        confirmed;
      checksum256     previous_block_id;

      _ds >> timestamp >> producer >> confirmed >> previous_block_id;
      (void)confirmed; // Only to suppress warning since confirmed is not used.

      // Add latest block information to blockinfo table.
      add_to_blockinfo_table(previous_block_id, timestamp);
      accumulate_gas_node_stats(timestamp.slot);

      #ifdef ENABLE_VOTING_PRODUCER
      /** check producer reward started */
      if( _gstate.election_activated_time == time_point() || timestamp < _gstate.election_activated_time )
         return;



      time_point now = timestamp;

      if (_gstate.reward_started_time != time_point() && now >= _gstate.reward_started_time) {
         record_producer_slot_stats(timestamp);
      }

      // Epoch settlement is intentionally transaction-driven. It performs
      // reward transfers and table scans that must never delay block production
      // or make every speculative onblock repeat a failing settlement.

      /**
       * At startup the initial producer may not be one that is registered / elected
       * and therefore there may be no producer object for them.
       */
      auto prod = _producers.find( producer.value );
      if ( prod != _producers.end() && _gstate.reward_started_time != time_point() && now >= _gstate.reward_started_time ) {
         // Preserve the live-chain global-row layout by reusing the deprecated
         // initial reward amount as an internal counter. Old non-negative values
         // mean "not migrated"; a negative amount encodes the number of reward-
         // eligible blocks already processed.
         const int64_t mining_cap = mining_reward_cap_amount(core_symbol());
         const uint64_t rewarded_block_count = _gstate.initial_rewards_per_block.amount < 0
            ? static_cast<uint64_t>(-_gstate.initial_rewards_per_block.amount)
            : rewarded_block_count_from_issued(_gstate.total_produced_rewards.amount, mining_cap);
         const int64_t remaining = std::max<int64_t>(0, mining_cap - _gstate.total_produced_rewards.amount);
         auto rewards_per_block = std::min(
            scheduled_reward_for_block(rewarded_block_count, mining_cap), remaining);
         check(rewarded_block_count < static_cast<uint64_t>(std::numeric_limits<int64_t>::max()),
               "rewarded block counter overflow");
         _gstate.initial_rewards_per_block.amount =
            -static_cast<int64_t>(rewarded_block_count + 1);

         // Very late eras can contain blocks with a zero smallest-unit reward.
         // They still advance the deterministic reward-block schedule, but do
         // not create empty epoch rows or alter reward totals.
         if (rewards_per_block > 0) {
            const uint64_t epoch_id = current_reward_epoch(now);
            epoch_rewards_table epoch_rewards(get_self(), epoch_id);
            auto reward_itr = epoch_rewards.find(producer.value);
            if (reward_itr == epoch_rewards.end()) {
               epoch_rewards.emplace(get_self(), [&](auto& row) {
                  row.producer = producer;
                  row.rewards = asset(rewards_per_block, core_symbol());
                  row.reward_shared_ratio = prod->reward_shared_ratio;
               });
            } else {
               epoch_rewards.modify(reward_itr, same_payer, [&](auto& row) {
                  row.rewards.amount += rewards_per_block;
               });
            }
            _gstate.total_produced_rewards.amount += rewards_per_block;
            _gstate.total_unclaimed_rewards.amount += rewards_per_block;
         }
      }

      /// only update block producers once every minute, block_timestamp is in half seconds
      if( timestamp.slot - _gstate.last_producer_schedule_update.slot > 120 ) {
         update_elected_producers( timestamp );

         #ifdef ENABLE_NAME_BID
         if( (timestamp.slot - _gstate.last_name_close.slot) > blocks_per_day ) {
            name_bid_table bids(get_self(), get_self().value);
            auto idx = bids.get_index<"highbid"_n>();
            auto highest = idx.lower_bound( std::numeric_limits<uint64_t>::max()/2 );
            if( highest != idx.end() &&
                highest->high_bid > 0 &&
                (current_time_point() - highest->last_bid_time) > microseconds(useconds_per_day) ) {
               _gstate.last_name_close = timestamp;
               channel_to_system_fees( names_account, asset( highest->high_bid, core_symbol() ) );
               // logging
               system_contract::logsystemfee_action logsystemfee_act{ get_self(), { {get_self(), active_permission} } };
               logsystemfee_act.send( names_account, asset( highest->high_bid, core_symbol() ), "buy name" );
               idx.modify( highest, same_payer, [&]( auto& b ){
                  b.high_bid = -b.high_bid;
               });
            }
         }
         #endif//ENABLE_NAME_BID
      }
      #endif//ENABLE_VOTING_PRODUCER
   }

   #ifdef ENABLE_VOTING_PRODUCER
   void system_contract::claimrewards( const name& owner ) {
      require_auth( owner );

      auto prod = _producers.find( owner.value );
      check( prod != _producers.end(), "producer not found" );
      check( prod->unclaimed_rewards.amount > 0, "no rewards to claim" );

      asset producer_rewards = prod->unclaimed_rewards;

      // Update producer/global state before inline transfers to avoid re-entrant duplicate claims.
      _producers.modify( prod, same_payer, [&](auto& p ) {
         p.unclaimed_rewards.amount = 0;
         p.last_claim_time = current_time_point();
      });
      _gstate.total_unclaimed_rewards.amount -= producer_rewards.amount;

      // Transfer producer rewards to owner.
      if (producer_rewards.amount > 0) {
         token::transfer_action transfer_act( token_account, {{get_self(), active_permission}} );
         transfer_act.send( get_self(), owner, producer_rewards, "producer rewards" );
      }
   }

   void system_contract::closeepoch( uint64_t epoch_id ) {
      check(_gstate.reward_started_time != time_point(), "producer rewards have not started");
      check(current_reward_epoch(current_time_point()) > epoch_id, "reward epoch has not ended");

      reward_epoch_singleton epoch_state_tbl(get_self(), get_self().value);
      auto epoch_state = epoch_state_tbl.exists() ? epoch_state_tbl.get() : reward_epoch_state{};
      check(epoch_id == epoch_state.next_epoch_to_close, "reward epochs must be closed sequentially");

      epoch_performance_table epochs(get_self(), get_self().value);
      auto perf_epoch_itr = epochs.find(epoch_id);
      const bool complete_sample = perf_epoch_itr != epochs.end() && perf_epoch_itr->complete_sample;

      bp_performance_table bp_stats(get_self(), epoch_id);
      uint32_t active_bp_count = 0;
      uint32_t failed_bp_count = 0;
      for (const auto& stats : bp_stats) {
         if (stats.expected_blocks == 0) continue;
         ++active_bp_count;
         if (complete_sample && stats.expected_blocks >= min_expected_blocks &&
             stats.produced_blocks * 100 < stats.expected_blocks * 90) {
            ++failed_bp_count;
         }
      }
      const bool network_incident = complete_sample && active_bp_count > 0 &&
                                    static_cast<uint64_t>(failed_bp_count) * 3 > active_bp_count;

      producer_performance_state_table producer_states(get_self(), get_self().value);
      for (auto stats_itr = bp_stats.begin(); stats_itr != bp_stats.end(); ++stats_itr) {
         auto status = bp_performance_status::qualified;
         if (!complete_sample || stats_itr->expected_blocks < min_expected_blocks) {
            status = bp_performance_status::insufficient_sample;
         } else if (network_incident) {
            status = bp_performance_status::network_incident;
         } else if (stats_itr->produced_blocks * 100 < stats_itr->expected_blocks * 90) {
            status = bp_performance_status::suspended;
         } else if (stats_itr->produced_blocks * 100 < stats_itr->expected_blocks * 95) {
            status = bp_performance_status::warning;
         } else if (stats_itr->produced_blocks * 100 < stats_itr->expected_blocks * 99) {
            status = bp_performance_status::degraded;
         }

         auto state_itr = producer_states.find(stats_itr->producer.value);
         const bool was_probation = state_itr != producer_states.end() &&
            state_itr->status == static_cast<uint8_t>(bp_performance_status::probation);
         // An incomplete sample or a network-wide incident does not prove that a
         // resumed producer has completed probation. Keep probation active until
         // the producer has a complete, individually classifiable epoch.
         if (was_probation && (status == bp_performance_status::insufficient_sample ||
                               status == bp_performance_status::network_incident)) {
            status = bp_performance_status::probation;
         }
         const bool already_suspended = state_itr != producer_states.end() &&
            state_itr->status == static_cast<uint8_t>(bp_performance_status::suspended);
         if (already_suspended) status = bp_performance_status::suspended;
         uint32_t warning_count = state_itr == producer_states.end() ? 0 : state_itr->warning_count;
         if (status == bp_performance_status::warning ||
             (status == bp_performance_status::suspended && !already_suspended))
            ++warning_count;
         uint32_t suspension_count = state_itr == producer_states.end() ? 0 : state_itr->suspension_count;
         if (status == bp_performance_status::suspended && !already_suspended) ++suspension_count;
         const uint64_t suspended_until = status == bp_performance_status::suspended
            ? (already_suspended ? state_itr->suspended_until_epoch
                                 : epoch_id + suspension_cooldown_epochs(suspension_count) + 1)
            : (state_itr == producer_states.end() ? 0 : state_itr->suspended_until_epoch);

         if (state_itr == producer_states.end()) {
            producer_states.emplace(get_self(), [&](auto& row) {
               row.producer = stats_itr->producer;
               row.status = static_cast<uint8_t>(status);
               row.warning_count = warning_count;
               row.suspended_until_epoch = suspended_until;
               row.suspension_count = suspension_count;
               row.probation_started_epoch = 0;
               row.probation_missed_streak = 0;
            });
         } else {
            producer_states.modify(state_itr, same_payer, [&](auto& row) {
               row.status = static_cast<uint8_t>(status);
               row.warning_count = warning_count;
               row.suspended_until_epoch = suspended_until;
               row.suspension_count = suspension_count;
               if (status != bp_performance_status::probation)
                  row.probation_started_epoch = 0;
               if (status != bp_performance_status::probation)
                  row.probation_missed_streak = 0;
            });
         }
         bp_stats.modify(stats_itr, same_payer, [&](auto& row) {
            row.status = static_cast<uint8_t>(status);
            row.warning_count = warning_count;
         });
      }

      if (perf_epoch_itr == epochs.end()) {
         const block_timestamp reward_start(_gstate.reward_started_time);
         const uint64_t epoch_start = static_cast<uint64_t>(reward_start.slot) +
                                      epoch_id * reward_epoch_slots;
         epochs.emplace(get_self(), [&](auto& row) {
            row.epoch_id = epoch_id;
            row.start_slot = static_cast<uint32_t>(epoch_start);
            row.end_slot = static_cast<uint32_t>(epoch_start + reward_epoch_slots - 1);
            row.complete_sample = false;
            row.active_bp_count = active_bp_count;
            row.failed_bp_count = failed_bp_count;
            row.network_incident = network_incident;
            row.closed = true;
         });
      } else {
         epochs.modify(perf_epoch_itr, same_payer, [&](auto& row) {
            row.active_bp_count = active_bp_count;
            row.failed_bp_count = failed_bp_count;
            row.network_incident = network_incident;
            row.closed = true;
         });
      }

      epoch_rewards_table epoch_rewards(get_self(), epoch_id);
      for (auto itr = epoch_rewards.begin(); itr != epoch_rewards.end();) {
         const auto producer_name = itr->producer;
         const auto gross_rewards = itr->rewards;
         auto prod = _producers.find(producer_name.value);
         check(prod != _producers.end(), "epoch reward producer not found");

         const auto stats_itr = bp_stats.find(producer_name.value);
         const bp_performance* producer_stats = stats_itr == bp_stats.end() ? nullptr : &*stats_itr;
         const uint32_t reward_factor = reward_factor_basis_points(producer_stats, network_incident);
         asset eligible_rewards(
            static_cast<int64_t>(static_cast<int128_t>(gross_rewards.amount) * reward_factor / ratio_boost),
            gross_rewards.symbol);
         const asset forfeited_rewards = gross_rewards - eligible_rewards;

         asset shared_rewards(0, gross_rewards.symbol);
         if (itr->reward_shared_ratio > 0) {
            const int128_t shared_amount = static_cast<int128_t>(eligible_rewards.amount) *
                                           itr->reward_shared_ratio / 10000;
            check(shared_amount <= std::numeric_limits<int64_t>::max(),
                  "shared reward overflow");
            shared_rewards.amount = static_cast<int64_t>(shared_amount);
         }
         const asset producer_rewards = eligible_rewards - shared_rewards;

         if (producer_rewards.amount > 0) {
            _producers.modify(prod, same_payer, [&](auto& p) {
               p.unclaimed_rewards += producer_rewards;
            });
         }

         if (shared_rewards.amount > 0) {
            token::transfer_action transfer_act(token_account, {{get_self(), active_permission}});
            transfer_act.send(get_self(), voter_reward_account, shared_rewards,
                              producer_name.to_string());
            _gstate.total_unclaimed_rewards.amount -= shared_rewards.amount;
         }

         if (forfeited_rewards.amount > 0) {
            _gstate.total_unclaimed_rewards.amount -= forfeited_rewards.amount;
         }

         itr = epoch_rewards.erase(itr);
      }

      flon::voter_reward::closeepoch_action close_reward_epoch{
         voter_reward_account, {{get_self(), active_permission}}
      };
      close_reward_epoch.send(epoch_id);

      // Ranking changes submitted during epoch N become active only after N
      // has been rewarded with its immutable vote snapshot.
      pending_vote_deltas_table vote_deltas(get_self(), get_self().value);
      for (auto delta_itr = vote_deltas.begin(); delta_itr != vote_deltas.end();) {
         auto prod = _producers.find(delta_itr->producer.value);
         check(prod != _producers.end(), "pending vote producer not found");
         _producers.modify(prod, same_payer, [&](auto& p) {
            p.total_votes += delta_itr->votes_delta;
            check(p.total_votes >= 0, "producer's elected votes can not be negative");
         });
         delta_itr = vote_deltas.erase(delta_itr);
      }

      epoch_state.next_epoch_to_close += 1;
      epoch_state_tbl.set(epoch_state, get_self());
   }

   void system_contract::setprodcfg( const time_point& schedule_start, const time_point& reward_epoch_start,
                                     const asset& initial_block_reward ) {
      require_auth(get_self());

      check(initial_block_reward.symbol == core_symbol(),
            "deprecated block reward field must use the core symbol");
      check(initial_block_reward.amount == 0,
            "initial block reward is fixed by protocol; compatibility field must be zero");

      const auto& now = eosio::current_time_point();
      if (_gstate.election_activated_time == time_point() || _gstate.election_activated_time > now ) {
         check(schedule_start > now, "producer schedule start must be in the future");
         _gstate.election_activated_time = schedule_start;
      } else {
         check( schedule_start == _gstate.election_activated_time,
            "producer schedule start cannot change after activation");
      }

      check(reward_epoch_start >= _gstate.election_activated_time,
            "reward epoch start cannot precede producer schedule start");
      if (_gstate.reward_started_time == time_point() || _gstate.reward_started_time > now ) {
         check(reward_epoch_start > now, "reward epoch start must be in the future");
         _gstate.reward_started_time = reward_epoch_start;
         // This legacy field remains serialized for live-chain compatibility;
         // the fixed protocol schedule no longer reads a configurable reward.
         _gstate.initial_rewards_per_block = asset(0, core_symbol());
      } else {
         check( reward_epoch_start == _gstate.reward_started_time,
            "reward epoch start cannot change after reward accounting has started");
      }
   }
   #endif//ENABLE_VOTING_PRODUCER
} //namespace eosiosystem

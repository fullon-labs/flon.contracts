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
      return reward_epoch_for_slot(now_slot.slot);
   }

   uint64_t system_contract::reward_epoch_for_slot( uint32_t slot ) const {
      check(_gstate.reward_started_time != time_point(), "producer rewards have not started");
      const block_timestamp start_slot(_gstate.reward_started_time);
      check(slot >= start_slot.slot, "slot is before producer rewards started");
      reward_epoch_config_v2_singleton config(get_self(), get_self().value);
      if (config.exists()) {
         const auto value = config.get();
         if (slot >= value.start_slot) {
            return value.first_epoch_id +
                   (static_cast<uint64_t>(slot) - value.start_slot) / value.epoch_slots;
         }
      }
      return (static_cast<uint64_t>(slot) - start_slot.slot) /
             legacy_reward_epoch_slots;
   }

   uint32_t system_contract::reward_epoch_start_slot( uint64_t epoch_id ) const {
      const block_timestamp legacy_start(_gstate.reward_started_time);
      reward_epoch_config_v2_singleton config(get_self(), get_self().value);
      uint64_t result = 0;
      if (config.exists() && epoch_id >= config.get().first_epoch_id) {
         const auto value = config.get();
         const uint64_t delta = epoch_id - value.first_epoch_id;
         check(delta <= (std::numeric_limits<uint32_t>::max() - value.start_slot) /
                           value.epoch_slots,
               "reward epoch slot overflow");
         result = static_cast<uint64_t>(value.start_slot) +
                  delta * value.epoch_slots;
      } else {
         check(epoch_id <= (std::numeric_limits<uint32_t>::max() - legacy_start.slot) /
                              legacy_reward_epoch_slots,
               "reward epoch slot overflow");
         result = static_cast<uint64_t>(legacy_start.slot) +
                  epoch_id * legacy_reward_epoch_slots;
      }
      check(result <= std::numeric_limits<uint32_t>::max(), "reward epoch slot overflow");
      return static_cast<uint32_t>(result);
   }

   uint32_t system_contract::reward_epoch_end_slot( uint64_t epoch_id ) const {
      reward_epoch_config_v2_singleton config(get_self(), get_self().value);
      const uint32_t slots = config.exists() && epoch_id >= config.get().first_epoch_id
         ? config.get().epoch_slots : legacy_reward_epoch_slots;
      const uint64_t result = static_cast<uint64_t>(reward_epoch_start_slot(epoch_id)) + slots - 1;
      check(result <= std::numeric_limits<uint32_t>::max(), "reward epoch slot overflow");
      return static_cast<uint32_t>(result);
   }

   void system_contract::accumulate_producer_slot_stats( uint64_t epoch_id,
                                                          uint32_t from_slot,
                                                          uint32_t to_slot,
                                                          const std::vector<eosio::producer_slot_stats_v2>& samples ) {
      bp_performance_table bp_stats(get_self(), epoch_id);

      for (const auto& sample : samples) {
         auto itr = bp_stats.find(sample.producer.value);
         if (itr == bp_stats.end()) {
            bp_stats.emplace(get_self(), [&](auto& row) {
               row.producer = sample.producer;
               row.epoch_id = epoch_id;
               row.expected_blocks = sample.expected;
               row.produced_blocks = sample.produced;
               row.missed_blocks = sample.missed;
               row.idle_skipped_blocks = sample.idle_skipped;
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
               row.idle_skipped_blocks =
                  row.idle_skipped_blocks.value_or(0) + sample.idle_skipped;
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
         const uint32_t epoch_start = reward_epoch_start_slot(epoch_id);
         const auto active_producers = eosio::get_active_producers();
         const auto packed_schedule = eosio::pack(active_producers);
         const auto schedule_hash = eosio::sha256(packed_schedule.data(), packed_schedule.size());
         epochs.emplace(get_self(), [&](auto& row) {
            row.epoch_id = epoch_id;
            row.start_slot = static_cast<uint32_t>(epoch_start);
            row.end_slot = reward_epoch_end_slot(epoch_id);
            row.schedule_hash = schedule_hash;
            row.complete_sample = from_slot == epoch_start;
         });
      }
   }

   void system_contract::record_producer_slot_stats( const block_timestamp& timestamp ) {
      performance_accounting_singleton accounting(get_self(), get_self().value);
      auto state = accounting.exists() ? accounting.get() : performance_accounting_state{};

      static constexpr uint8_t performance_accounting_version = 2;
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

      while (from_slot <= timestamp.slot) {
         const uint64_t epoch_id = reward_epoch_for_slot(static_cast<uint32_t>(from_slot));
         const uint64_t epoch_end = reward_epoch_end_slot(epoch_id);
         uint64_t query_end = std::min<uint64_t>(timestamp.slot, epoch_end);
         gas_reward_config_singleton gas_config_table(get_self(), get_self().value);
         if (gas_config_table.exists() && gas_config_table.get().enabled) {
            const auto gas_config = gas_config_table.get();
            const uint64_t gas_epoch = from_slot / gas_config.epoch_slots;
            const uint64_t gas_end = (gas_epoch + 1) * uint64_t(gas_config.epoch_slots) - 1;
            query_end = std::min(query_end, gas_end);
         }
         const auto samples = eosio::get_producer_slot_stats_v2(
            static_cast<uint32_t>(from_slot), static_cast<uint32_t>(query_end));
         accumulate_producer_slot_stats(epoch_id, static_cast<uint32_t>(from_slot),
                                        static_cast<uint32_t>(query_end), samples);
         accumulate_gas_slot_stats(static_cast<uint32_t>(from_slot),
                                   static_cast<uint32_t>(query_end), samples);
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

   void system_contract::classify_producer_epoch( uint64_t epoch_id,
                                                   const bp_performance& stats,
                                                   bool complete_sample,
                                                   bool network_incident ) {
      auto status = bp_performance_status::qualified;
      if (!complete_sample || stats.expected_blocks < min_expected_blocks) {
         status = bp_performance_status::insufficient_sample;
      } else if (network_incident) {
         status = bp_performance_status::network_incident;
      } else if (stats.produced_blocks * 100 < stats.expected_blocks * 90) {
         status = bp_performance_status::suspended;
      } else if (stats.produced_blocks * 100 < stats.expected_blocks * 95) {
         status = bp_performance_status::warning;
      } else if (stats.produced_blocks * 100 < stats.expected_blocks * 99) {
         status = bp_performance_status::degraded;
      }

      producer_performance_state_table states(get_self(), get_self().value);
      auto state_itr = states.find(stats.producer.value);
      const bool was_probation = state_itr != states.end() &&
         state_itr->status == static_cast<uint8_t>(bp_performance_status::probation);
      if (was_probation && (status == bp_performance_status::insufficient_sample ||
                            status == bp_performance_status::network_incident)) {
         status = bp_performance_status::probation;
      }
      const bool already_suspended = state_itr != states.end() &&
         state_itr->status == static_cast<uint8_t>(bp_performance_status::suspended);
      if (already_suspended) status = bp_performance_status::suspended;

      uint32_t warnings = state_itr == states.end() ? 0 : state_itr->warning_count;
      if (status == bp_performance_status::warning ||
          (status == bp_performance_status::suspended && !already_suspended)) ++warnings;
      uint32_t suspensions = state_itr == states.end() ? 0 : state_itr->suspension_count;
      if (status == bp_performance_status::suspended && !already_suspended) ++suspensions;
      const uint64_t suspended_until = status == bp_performance_status::suspended
         ? (already_suspended ? state_itr->suspended_until_epoch
                              : epoch_id + suspension_cooldown_epochs(suspensions) + 1)
         : (state_itr == states.end() ? 0 : state_itr->suspended_until_epoch);

      if (state_itr == states.end()) {
         states.emplace(get_self(), [&](auto& row) {
            row.producer = stats.producer;
            row.status = static_cast<uint8_t>(status);
            row.warning_count = warnings;
            row.suspended_until_epoch = suspended_until;
            row.suspension_count = suspensions;
            row.probation_started_epoch = 0;
            row.probation_missed_streak = 0;
         });
      } else {
         states.modify(state_itr, same_payer, [&](auto& row) {
            row.status = static_cast<uint8_t>(status);
            row.warning_count = warnings;
            row.suspended_until_epoch = suspended_until;
            row.suspension_count = suspensions;
            if (status != bp_performance_status::probation) {
               row.probation_started_epoch = 0;
               row.probation_missed_streak = 0;
            }
         });
      }

      bp_performance_table epoch_stats(get_self(), epoch_id);
      auto epoch_itr = epoch_stats.find(stats.producer.value);
      if (epoch_itr != epoch_stats.end()) {
         epoch_stats.modify(epoch_itr, same_payer, [&](auto& row) {
            row.status = static_cast<uint8_t>(status);
            row.warning_count = warnings;
         });
      }
   }

   void system_contract::advance_reward_epoch_seal( uint16_t max_rows ) {
      check(max_rows > 0 && max_rows <= 30, "seal row limit must be between 1 and 30");
      if (_gstate.reward_started_time == time_point() ||
          current_time_point() < _gstate.reward_started_time) return;

      reward_epoch_singleton epoch_state_table(get_self(), get_self().value);
      auto epoch_state = epoch_state_table.exists() ? epoch_state_table.get() : reward_epoch_state{};
      if (current_reward_epoch(current_time_point()) <= epoch_state.next_epoch_to_close) return;

      reward_seal_singleton seal_table(get_self(), get_self().value);
      auto seal = seal_table.exists() ? seal_table.get() : reward_seal_state{};
      if (!seal_table.exists() ||
          (seal.phase == static_cast<uint8_t>(reward_seal_phase::sealed) &&
           seal.epoch_id < epoch_state.next_epoch_to_close)) {
         seal = reward_seal_state{};
         seal.epoch_id = epoch_state.next_epoch_to_close;
         seal.phase = static_cast<uint8_t>(reward_seal_phase::ending);
      }
      check(seal.epoch_id == epoch_state.next_epoch_to_close,
            "reward seal cursor does not match epoch cursor");

      epoch_performance_table epochs(get_self(), get_self().value);
      auto performance_epoch = epochs.find(seal.epoch_id);
      const bool complete_sample = performance_epoch != epochs.end() &&
                                   performance_epoch->complete_sample;
      bp_performance_table stats(get_self(), seal.epoch_id);
      uint16_t processed = 0;

      if (seal.phase == static_cast<uint8_t>(reward_seal_phase::ending) && seal.stage == 0) {
         auto itr = seal.cursor.value ? stats.upper_bound(seal.cursor.value) : stats.begin();
         while (itr != stats.end() && processed < max_rows) {
            if (itr->expected_blocks > 0) {
               ++seal.active_bp_count;
               if (complete_sample && itr->expected_blocks >= min_expected_blocks &&
                   itr->produced_blocks * 100 < itr->expected_blocks * 90) {
                  ++seal.failed_bp_count;
               }
            }
            seal.cursor = itr->producer;
            ++itr;
            ++processed;
         }
         if (itr == stats.end()) {
            seal.network_incident = complete_sample && seal.active_bp_count > 0 &&
               static_cast<uint64_t>(seal.failed_bp_count) * 3 > seal.active_bp_count;
            seal.stage = 1;
            seal.cursor = name{};
         }
      }

      if (seal.phase == static_cast<uint8_t>(reward_seal_phase::ending) &&
          seal.stage == 1 && processed < max_rows) {
         auto itr = seal.cursor.value ? stats.upper_bound(seal.cursor.value) : stats.begin();
         while (itr != stats.end() && processed < max_rows) {
            const auto snapshot = *itr;
            classify_producer_epoch(seal.epoch_id, snapshot, complete_sample,
                                    seal.network_incident);
            seal.cursor = itr->producer;
            ++itr;
            ++processed;
         }
         if (itr == stats.end()) {
            if (performance_epoch == epochs.end()) {
               epochs.emplace(get_self(), [&](auto& row) {
                  row.epoch_id = seal.epoch_id;
                  row.start_slot = reward_epoch_start_slot(seal.epoch_id);
                  row.end_slot = reward_epoch_end_slot(seal.epoch_id);
                  row.complete_sample = false;
                  row.active_bp_count = seal.active_bp_count;
                  row.failed_bp_count = seal.failed_bp_count;
                  row.network_incident = seal.network_incident;
                  row.closed = false;
               });
            } else {
               epochs.modify(performance_epoch, same_payer, [&](auto& row) {
                  row.active_bp_count = seal.active_bp_count;
                  row.failed_bp_count = seal.failed_bp_count;
                  row.network_incident = seal.network_incident;
               });
            }
            seal.stage = 2;
            seal.cursor = name{};
         }
      }

      if (seal.phase == static_cast<uint8_t>(reward_seal_phase::ending) &&
          seal.stage == 2 && processed < max_rows) {
         epoch_rewards_table rewards(get_self(), seal.epoch_id);
         auto itr = seal.cursor.value ? rewards.upper_bound(seal.cursor.value) : rewards.begin();
         while (itr != rewards.end() && processed < max_rows) {
            const auto producer_name = itr->producer;
            const auto gross = itr->rewards;
            auto producer_itr = _producers.find(producer_name.value);
            check(producer_itr != _producers.end(), "epoch reward producer not found");
            auto stat_itr = stats.find(producer_name.value);
            const bp_performance* producer_stats = stat_itr == stats.end() ? nullptr : &*stat_itr;
            const uint32_t factor = reward_factor_basis_points(producer_stats,
                                                                seal.network_incident);
            asset eligible(static_cast<int64_t>(static_cast<int128_t>(gross.amount) *
                                                 factor / ratio_boost), gross.symbol);
            asset shared(0, gross.symbol);
            if (itr->reward_shared_ratio > 0) {
               shared.amount = static_cast<int64_t>(static_cast<int128_t>(eligible.amount) *
                                                    itr->reward_shared_ratio / 10000);
            }
            const asset producer_reward = eligible - shared;
            const asset forfeited = gross - eligible;
            if (producer_reward.amount > 0) {
               producer_epoch_claims_table claim_rows(get_self(), producer_name.value);
               check(claim_rows.find(seal.epoch_id) == claim_rows.end(),
                     "duplicate producer epoch claim row");
               claim_rows.emplace(get_self(), [&](auto& row) {
                  row.epoch_id = seal.epoch_id;
                  row.quantity = producer_reward;
               });
               _producers.modify(producer_itr, same_payer, [&](auto& row) {
                  row.unclaimed_rewards += producer_reward;
               });
            }
            if (shared.amount > 0) {
               token::transfer_action(token_account, {{get_self(), active_permission}})
                  .send(get_self(), voter_reward_account, shared, producer_name.to_string());
               _gstate.total_unclaimed_rewards.amount -= shared.amount;
            }
            if (forfeited.amount > 0) _gstate.total_unclaimed_rewards.amount -= forfeited.amount;
            itr = rewards.erase(itr);
            ++processed;
         }
         if (itr == rewards.end()) {
            seal.phase = static_cast<uint8_t>(reward_seal_phase::applying_deltas);
            seal.stage = 0;
            seal.cursor = name{};
         }
      }

      if (seal.phase == static_cast<uint8_t>(reward_seal_phase::applying_deltas) &&
          processed < max_rows) {
         pending_vote_deltas_table deltas(get_self(), get_self().value);
         auto delta = deltas.begin();
         while (delta != deltas.end() && processed < max_rows) {
            auto producer_itr = _producers.find(delta->producer.value);
            check(producer_itr != _producers.end(), "pending vote producer not found");
            _producers.modify(producer_itr, same_payer, [&](auto& row) {
               row.total_votes += delta->votes_delta;
               check(row.total_votes >= 0, "producer's elected votes can not be negative");
            });
            delta = deltas.erase(delta);
            ++processed;
         }

         flon::voter_reward::epoch_state::table voter_epoch_state(
            voter_reward_account, voter_reward_account.value);
         auto voter_state = voter_epoch_state.exists()
            ? voter_epoch_state.get() : flon::voter_reward::epoch_state{};
         if (voter_state.next_epoch_to_close == seal.epoch_id) {
            flon::voter_reward::sealepoch_action seal_voter_epoch{
               voter_reward_account, {{get_self(), active_permission}}
            };
            seal_voter_epoch.send(seal.epoch_id, max_rows);
            voter_state = voter_epoch_state.exists()
               ? voter_epoch_state.get() : flon::voter_reward::epoch_state{};
         }
         if (deltas.begin() == deltas.end() &&
             voter_state.next_epoch_to_close > seal.epoch_id) {
            auto epoch_itr = epochs.find(seal.epoch_id);
            if (epoch_itr != epochs.end()) {
               epochs.modify(epoch_itr, same_payer, [&](auto& row) { row.closed = true; });
            }
            seal.phase = static_cast<uint8_t>(reward_seal_phase::sealed);
            epoch_state.next_epoch_to_close += 1;
            epoch_state_table.set(epoch_state, get_self());
         }
      }

      seal_table.set(seal, get_self());
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
         advance_reward_epoch_seal(1);
      }

      advance_gas_settlement(timestamp.slot);

      // Epoch sealing is advanced by bounded onblock steps. No external
      // settlement transaction is required for protocol correctness.

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

      // Never build a producer schedule from a partially applied vote-delta
      // boundary. The next schedule update runs after the epoch is SEALED.
      reward_seal_singleton schedule_seal_table(get_self(), get_self().value);
      const bool vote_boundary_in_progress = schedule_seal_table.exists() &&
         schedule_seal_table.get().phase != static_cast<uint8_t>(reward_seal_phase::sealed);

      /// only update block producers once every minute, block_timestamp is in half seconds
      if( !vote_boundary_in_progress &&
          timestamp.slot - _gstate.last_producer_schedule_update.slot > 120 ) {
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
   void system_contract::claimrewards( const name& caller, const name& producer,
                                       uint16_t max_steps ) {
      require_auth(caller);
      check(max_steps > 0 && max_steps <= 48, "max_steps must be between 1 and 48");

      auto prod = _producers.find(producer.value);
      check( prod != _producers.end(), "producer not found" );
      gas_epoch_claims_table gas_claim_rows(get_self(), producer.value);
      const bool has_base = prod->unclaimed_rewards.amount > 0;
      const bool has_gas = gas_claim_rows.begin() != gas_claim_rows.end();
      check(has_base || has_gas, "all rewards claimed");

      asset producer_rewards(0, core_symbol());
      producer_epoch_claims_table claim_rows(get_self(), producer.value);
      uint16_t processed = 0;
      auto claim_row = claim_rows.begin();
      while (claim_row != claim_rows.end() && processed < max_steps) {
         producer_rewards += claim_row->quantity;
         claim_row = claim_rows.erase(claim_row);
         ++processed;
      }
      // Rewards accumulated before prodclaims existed are consumed as one
      // legacy step once all explicit hourly rows have been drained.
      if (has_base && processed == 0) {
         producer_rewards = prod->unclaimed_rewards;
      }

      if (producer_rewards.amount > 0) {
         _producers.modify(prod, same_payer, [&](auto& row) {
            row.unclaimed_rewards -= producer_rewards;
            row.last_claim_time = current_time_point();
         });
         _gstate.total_unclaimed_rewards.amount -= producer_rewards.amount;

         token::transfer_action transfer_act( token_account, {{get_self(), active_permission}} );
         transfer_act.send(get_self(), producer, producer_rewards, "producer base rewards");
      }
      if (has_gas) {
         asset gas_rewards(0, core_symbol());
         uint16_t gas_steps = 0;
         auto gas_claim = gas_claim_rows.begin();
         while (gas_claim != gas_claim_rows.end() && gas_steps < max_steps) {
            gas_rewards += gas_claim->quantity;
            gas_claim = gas_claim_rows.erase(gas_claim);
            ++gas_steps;
         }
         if (gas_rewards.amount > 0) {
            token::transfer_action(token_account, {{gas_account, active_permission}})
               .send(gas_account, producer, gas_rewards, "producer GAS rewards");
         }
      }
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
         reward_epoch_config_v2_singleton reward_config(get_self(), get_self().value);
         if (!reward_config.exists()) {
            const block_timestamp start_slot(reward_epoch_start);
            reward_config.set({0, start_slot.slot, reward_epoch_slots}, get_self());
         }
         // This legacy field remains serialized for live-chain compatibility;
         // the fixed protocol schedule no longer reads a configurable reward.
         _gstate.initial_rewards_per_block = asset(0, core_symbol());
      } else {
         check( reward_epoch_start == _gstate.reward_started_time,
            "reward epoch start cannot change after reward accounting has started");
      }
   }

   void system_contract::setrwdcfg( const time_point& start_time ) {
      require_auth(get_self());
      check(_gstate.reward_started_time != time_point(), "producer rewards have not been configured");

      reward_epoch_config_v2_singleton config(get_self(), get_self().value);
      check(!config.exists(), "hourly reward epochs are already configured");

      const block_timestamp start_slot(start_time);
      const block_timestamp now_slot(current_time_point());
      const block_timestamp legacy_start(_gstate.reward_started_time);
      check(start_slot.slot > now_slot.slot, "hourly reward epoch start must be in the future");
      check(start_slot.slot >= legacy_start.slot, "hourly reward epoch start precedes rewards");

      const uint64_t offset = static_cast<uint64_t>(start_slot.slot) - legacy_start.slot;
      check(offset % legacy_reward_epoch_slots == 0,
            "hourly reward epochs must start on a legacy epoch boundary");
      const uint64_t first_epoch_id = offset / legacy_reward_epoch_slots;

      reward_epoch_singleton state_table(get_self(), get_self().value);
      const auto state = state_table.exists() ? state_table.get() : reward_epoch_state{};
      check(state.next_epoch_to_close <= first_epoch_id,
            "hourly reward boundary is earlier than the settlement cursor");

      config.set({first_epoch_id, start_slot.slot, reward_epoch_slots}, get_self());
   }
   #endif//ENABLE_VOTING_PRODUCER
} //namespace eosiosystem

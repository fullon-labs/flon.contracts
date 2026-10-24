#include <flon.system/flon.system.hpp>
#include <flon.token/flon.token.hpp>
#include <gas.reward/gas.reward.hpp>

#include <algorithm>

namespace eosiosystem {

namespace {
constexpr uint8_t gas_accounting_version = 1;
constexpr uint8_t active_role = 1;
constexpr uint8_t standby_role = 2;
constexpr uint16_t basis_points = 10'000;
constexpr uint16_t max_settle_rows = 10;
constexpr uint16_t standby_target_seats = 30;

bool contains(const std::vector<name>& values, name value) {
   return std::find(values.begin(), values.end(), value) != values.end();
}

int64_t mul_div(int64_t value, uint64_t multiplier, uint64_t divisor,
                const char* overflow_message) {
   const __int128 result = static_cast<__int128>(value) * multiplier / divisor;
   check(result >= 0 && result <= std::numeric_limits<int64_t>::max(), overflow_message);
   return static_cast<int64_t>(result);
}
}

void system_contract::setgasrwcfg(bool enabled, uint32_t epoch_slots,
                                  uint16_t active_min_bp, uint16_t pool_bp,
                                  uint16_t active_share_bp) {
   require_auth(get_self());
   check(epoch_slots >= 1'200, "epoch must contain at least 1200 slots");
   check(active_min_bp <= basis_points && pool_bp <= basis_points &&
         active_share_bp <= basis_points,
         "basis point value exceeds 10000");
   gas_reward_config_singleton config(get_self(), get_self().value);
   if (config.exists()) {
      const auto old = config.get();
      if (old.enabled) {
         check(!enabled, "disable gas rewards before changing configuration");
         check(epoch_slots == old.epoch_slots && active_min_bp == old.active_min_bp &&
               pool_bp == old.pool_bp &&
               active_share_bp == old.active_share_bp,
               "configuration cannot change while enabled");
      } else if (enabled && epoch_slots != old.epoch_slots) {
         // A disabled configuration may move to the governed hourly cadence.
         // Historical epochs retain their own gasepochcfg rows; the new
         // absolute-slot epoch numbering starts strictly after re-enablement.
         check(epoch_slots == reward_epoch_slots,
               "gas reward epoch migration must use the one-hour cadence");
      }
   }

   const uint64_t first_reward_epoch = enabled
      ? uint64_t(eosio::current_block_time().slot) / epoch_slots + 1
      : 0;
   gas_reward_config next{enabled, epoch_slots, active_min_bp, 0,
                          pool_bp, active_share_bp, 0,
                          0, 0, gas_accounting_version, first_reward_epoch};
   config.set(next, get_self());
   gas_auto_state_singleton auto_state(get_self(), get_self().value);
   if (enabled) {
      auto_state.set({first_reward_epoch, true}, get_self());
   } else if (auto_state.exists()) {
      auto_state.remove();
   }
}

void system_contract::ensure_gas_epoch_roles(uint64_t epoch_id,
                                             const gas_reward_config& config) {
   gas_epoch_configs epoch_configs(get_self(), get_self().value);
   if (epoch_configs.find(epoch_id) == epoch_configs.end()) {
      epoch_configs.emplace(get_self(), [&](auto& row) {
         row.epoch_id = epoch_id;
         row.epoch_slots = config.epoch_slots;
         row.active_min_bp = config.active_min_bp;
         row.reserved_standby_proofs = 0;
         row.pool_bp = config.pool_bp;
         row.active_share_bp = config.active_share_bp;
         row.reserved_reward_bp = 0;
      });
   }
   gas_node_stats stats(get_self(), epoch_id);
   if (stats.begin() != stats.end()) return;

   const auto zero = asset(0, core_symbol());
   const auto add_role = [&](name owner, uint8_t role) {
      stats.emplace(get_self(), [&](auto& row) {
         row.owner = owner; row.role = role; row.allocated = zero;
      });
   };

   const auto active = eosio::get_active_producers();
   for (const auto owner : active) {
      add_role(owner, active_role);
   }

   auto by_votes = _producers.get_index<"prototalvote"_n>();
   bp_stake_config_singleton bp_stake_config_table(get_self(), get_self().value);
   const auto bp_stake_config = bp_stake_config_table.exists()
      ? std::optional<eosiosystem::bp_stake_config>(bp_stake_config_table.get())
      : std::nullopt;
   uint16_t standby_count = 0;
   uint32_t candidates_scanned = 0;
   for (auto it = by_votes.begin();
        it != by_votes.end() && standby_count < standby_target_seats &&
        candidates_scanned < max_schedule_candidates_scanned;
        ++it, ++candidates_scanned) {
      if (!it->is_active || contains(active, it->owner)) continue;
      if (!is_performance_eligible(it->owner, epoch_id)) continue;
      if (bp_stake_config && bp_stake_config->enforce_schedule &&
          !has_min_bp_self_stake(it->owner, *bp_stake_config)) continue;
      add_role(it->owner, standby_role);
      ++standby_count;
   }
}

void system_contract::accumulate_gas_node_stats(uint32_t current_slot) {
   gas_reward_config_singleton config_table(get_self(), get_self().value);
   if (!config_table.exists()) return;
   auto config = config_table.get();
   if (!config.enabled) return;

   const auto totals = eosio::get_gas_fee_totals();
   if (!config.first_reward_epoch.has_value()) {
      // A row written before reward gating has no tail extension.  Upgrade it
      // in place without interpreting historical GAS or slots as rewardable.
      config.first_reward_epoch =
         uint64_t(current_slot) / config.epoch_slots + 1;
      config.last_accounted_slot = current_slot;
      config.last_metered_gas_value = totals.total_gas_value_flon;
      config.accounting_version = gas_accounting_version;
      config_table.set(config, get_self());
      return;
   }
   const uint64_t first_reward_epoch = config.first_reward_epoch.value();
   const bool current_version = config.accounting_version.has_value() &&
                                config.accounting_version.value() == gas_accounting_version;
   if (!current_version || config.last_accounted_slot == 0 ||
       config.last_accounted_slot >= current_slot) {
      // Bootstrap after deployment/upgrades instead of trusting a cursor from
      // an older accounting implementation.
      config.last_accounted_slot = current_slot;
      config.last_metered_gas_value = totals.total_gas_value_flon;
      config.accounting_version = gas_accounting_version;
      config_table.set(config, get_self());
      return;
   }
   check(current_slot > config.last_accounted_slot, "block slot must increase");
   check(totals.total_gas_value_flon >= config.last_metered_gas_value,
         "gas fee total cannot decrease");

   const uint64_t pending_slot_count =
      static_cast<uint64_t>(current_slot) - config.last_accounted_slot;
   if (pending_slot_count > producer_stats_onblock_limit) {
      // Never reconstruct an arbitrarily large missed-slot range from onblock.
      // The gas delta cannot be assigned to node performance fairly after such
      // a gap, so re-bootstrap both baselines and leave the interval unrewarded.
      config.last_accounted_slot = current_slot;
      config.last_metered_gas_value = totals.total_gas_value_flon;
      config_table.set(config, get_self());
      return;
   }

   // The delta visible at the start of this block belongs to the previously
   // accounted block, not to the current epoch's pending block.
   const uint64_t gas_epoch = config.last_accounted_slot / config.epoch_slots;
   const uint64_t gas_delta = totals.total_gas_value_flon - config.last_metered_gas_value;
   if (gas_epoch >= first_reward_epoch) {
      gas_epoch_meters meters(get_self(), get_self().value);
      auto meter = meters.find(gas_epoch);
      if (meter == meters.end()) {
         meters.emplace(get_self(), [&](auto& row) { row.epoch_id = gas_epoch; row.gas_value = gas_delta; });
      } else {
         meters.modify(meter, eosio::same_payer, [&](auto& row) { row.gas_value += gas_delta; });
      }
   }

   config.last_accounted_slot = current_slot;
   config.last_metered_gas_value = totals.total_gas_value_flon;
   config_table.set(config, get_self());
}

void system_contract::accumulate_gas_slot_stats(
      uint32_t from_slot, uint32_t to_slot,
      const std::vector<eosio::producer_slot_stats_v2>& samples) {
   gas_reward_config_singleton config_table(get_self(), get_self().value);
   if (!config_table.exists()) return;
   const auto config = config_table.get();
   if (!config.enabled || !config.first_reward_epoch.has_value()) return;

   const uint64_t epoch_id = uint64_t(from_slot) / config.epoch_slots;
   check(uint64_t(to_slot) / config.epoch_slots == epoch_id,
         "shared slot-stat sample crosses a GAS epoch boundary");
   if (epoch_id < config.first_reward_epoch.value()) return;

   ensure_gas_epoch_roles(epoch_id, config);
   gas_node_stats stats(get_self(), epoch_id);
   for (const auto& item : samples) {
      auto stat = stats.find(item.producer.value);
      if (stat == stats.end()) continue;
      stats.modify(stat, eosio::same_payer, [&](auto& row) {
         row.expected += item.expected;
         row.produced += item.produced;
         row.missed += item.missed;
         row.idle_skipped = row.idle_skipped.value_or(0) + item.idle_skipped;
         row.longest_missed = std::max(row.longest_missed,
                                       item.longest_missed_streak);
      });
   }
}

void system_contract::beginsettle(uint64_t epoch_id) {
   gas_reward_config_singleton config_table(get_self(), get_self().value);
   check(config_table.exists() && config_table.get().enabled, "gas rewards are disabled");
   const auto config = config_table.get();
   check(epoch_id < eosio::current_block_time().slot / config.epoch_slots, "epoch has not ended");
   gas_epoch_configs epoch_configs(get_self(), get_self().value);
   const auto epoch_config = epoch_configs.get(epoch_id, "missing epoch reward configuration");
   gas_epoch_meters meters(get_self(), get_self().value);
   auto meter = meters.find(epoch_id);
   if (meter == meters.end()) {
      meters.emplace(get_self(), [&](auto& row) {
         row.epoch_id = epoch_id;
         row.gas_value = 0;
      });
      meter = meters.find(epoch_id);
   }
   gas_settlements settlements(get_self(), get_self().value);
   check(settlements.find(epoch_id) == settlements.end(), "settlement already started");
   const auto sym = core_symbol();
   const asset zero(0, sym);
   check(meter->gas_value <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max()),
         "epoch gas value exceeds asset range");
   const int64_t pool_amount = mul_div(static_cast<int64_t>(meter->gas_value), epoch_config.pool_bp,
                                       basis_points, "epoch reward pool overflow");
   settlements.emplace(get_self(), [&](auto& row) {
      row.epoch_id = epoch_id; row.pool = asset(pool_amount, sym);
      row.active_pool = zero; row.standby_pool = zero;
      row.active_allocated = zero; row.standby_allocated = zero;
   });
}

void system_contract::settlenext(uint64_t epoch_id, uint16_t max_rows) {
   check(max_rows > 0 && max_rows <= max_settle_rows, "max_rows must be between 1 and 10");
   gas_epoch_configs epoch_configs(get_self(), get_self().value);
   const auto config = epoch_configs.get(epoch_id, "missing epoch reward configuration");
   gas_settlements settlements(get_self(), get_self().value);
   auto settlement = settlements.require_find(epoch_id, "settlement not started");
   check(!settlement->finalized, "settlement already finalized");
   gas_node_stats stats(get_self(), epoch_id);

   auto it = settlement->cursor.value ? stats.upper_bound(settlement->cursor.value) : stats.begin();
   uint16_t processed = 0;
   if (settlement->phase == 1) {
      uint64_t active_score = settlement->active_score;
      uint32_t active_total = settlement->active_total;
      uint32_t active_qualified = settlement->active_eligible;
      uint32_t active_participants = settlement->active_reward_participants.has_value()
         ? settlement->active_reward_participants.value()
         : 0;
      uint32_t standby_count = settlement->standby_eligible;
      name cursor = settlement->cursor;
      while (it != stats.end() && processed++ < max_rows) {
         if (it->role == active_role) ++active_total;
         const bool active_participated = it->role == active_role && it->produced > 0;
         const bool active_qualified_for_status = active_participated && it->expected > 0 &&
            uint64_t(it->produced) * basis_points >= uint64_t(it->expected) * config.active_min_bp;
         // RPC-derived standby proofs did not demonstrate that a candidate ran
         // a production-ready node. Until a consensus-backed readiness test is
         // introduced, the epoch snapshot itself defines Standby eligibility.
         const bool standby_ok = it->role == standby_role;
         const uint64_t score = active_participated ? it->produced : 0;
         stats.modify(it, eosio::same_payer, [&](auto& row) {
            row.eligible = active_participated || standby_ok; row.score = score;
         });
         if (active_participated) { active_score += score; ++active_participants; }
         if (active_qualified_for_status) ++active_qualified;
         if (standby_ok) ++standby_count;
         cursor = it->owner; ++it;
      }
      const bool done = it == stats.end();
      settlements.modify(settlement, eosio::same_payer, [&](auto& row) {
         row.active_score = active_score; row.active_total = active_total;
         row.active_eligible = active_qualified;
         row.active_reward_participants = active_participants;
         row.standby_eligible = standby_count; row.cursor = done ? name{} : cursor;
         if (done) {
            row.incident_hold = active_total > 0 &&
               uint64_t(active_qualified) * 3 < uint64_t(active_total) * 2;
            row.phase = row.incident_hold ? 4 : 2;
            // Every scheduled Active BP participates according to actual block
            // production. The configured Active share is released in full even
            // when the governed quorum is below the final 21-producer target.
            row.active_pool.amount = active_participants
               ? mul_div(row.pool.amount, config.active_share_bp, basis_points,
                         "active reward pool overflow")
               : 0;
            const uint16_t standby_share_bp = basis_points - config.active_share_bp;
            row.standby_pool.amount = standby_count
               ? mul_div(row.pool.amount, uint64_t(standby_share_bp) * standby_count,
                         uint64_t(basis_points) * standby_target_seats,
                         "standby reward pool overflow")
               : 0;
            row.pool.amount = row.active_pool.amount + row.standby_pool.amount;
         }
      });
      if (!done) return;

      settlement = settlements.find(epoch_id);
      if (settlement->incident_hold) return;
      settlements.modify(settlement, eosio::same_payer, [&](auto& row) { row.funded = true; });
      return;
   }

   check(settlement->funded, "settlement is not funded");
   name cursor = settlement->cursor;
   auto hash = settlement->allocation_hash;
   int64_t active_allocated = settlement->active_allocated.amount;
   int64_t standby_allocated = settlement->standby_allocated.amount;
   uint32_t active_paid = settlement->active_paid_count;
   uint32_t standby_paid = settlement->standby_paid_count;
   const uint32_t active_participants = settlement->active_reward_participants.has_value()
      ? settlement->active_reward_participants.value()
      : 0;
   while (it != stats.end() && processed++ < max_rows) {
      cursor = it->owner;
      if (!it->eligible) { ++it; continue; }
      int64_t amount = 0;
      if (it->role == active_role) {
         ++active_paid;
         if (active_paid == active_participants) {
            amount = settlement->active_pool.amount - active_allocated;
         } else {
            amount = mul_div(settlement->active_pool.amount, it->score,
                             settlement->active_score,
                             "active performance reward overflow");
         }
         check(amount <= settlement->active_pool.amount - active_allocated,
               "active allocation exceeds pool");
         active_allocated += amount;
      } else {
         ++standby_paid;
         amount = standby_paid == settlement->standby_eligible
            ? settlement->standby_pool.amount - standby_allocated
            : settlement->standby_pool.amount / settlement->standby_eligible;
         check(amount <= settlement->standby_pool.amount - standby_allocated,
               "standby allocation exceeds pool");
         standby_allocated += amount;
      }
      if (amount > 0) {
         const asset quantity(amount, core_symbol());
         gas_epoch_claims_table claims(get_self(), it->owner.value);
         check(claims.find(epoch_id) == claims.end(), "duplicate GAS epoch claim row");
         claims.emplace(get_self(), [&](auto& row) {
            row.epoch_id = epoch_id;
            row.quantity = quantity;
         });
         const auto packed = eosio::pack(std::make_tuple(hash, it->owner, quantity));
         hash = eosio::sha256(packed.data(), packed.size());
         stats.modify(it, eosio::same_payer, [&](auto& row) { row.allocated = quantity; });
      }
      ++it;
   }
   const bool done = it == stats.end();
   settlement = settlements.find(epoch_id);
   settlements.modify(settlement, eosio::same_payer, [&](auto& row) {
      row.cursor = done ? name{} : cursor; row.allocation_hash = hash;
      row.active_allocated.amount = active_allocated; row.standby_allocated.amount = standby_allocated;
      row.active_paid_count = active_paid; row.standby_paid_count = standby_paid;
      if (done) row.phase = 3;
   });
}

void system_contract::resolveincdt(uint64_t epoch_id, bool proceed) {
   require_auth(get_self());
   gas_settlements settlements(get_self(), get_self().value);
   auto settlement = settlements.require_find(epoch_id, "settlement not started");
   check(settlement->incident_hold && settlement->phase == 4 && !settlement->funded,
         "epoch is not held for an incident");
   if (!proceed) {
      settlements.modify(settlement, eosio::same_payer, [&](auto& row) {
         row.pool.amount = 0; row.active_pool.amount = 0; row.standby_pool.amount = 0;
         row.incident_hold = false; row.finalized = true;
      });
      return;
   }
   settlements.modify(settlement, eosio::same_payer, [&](auto& row) {
      row.phase = 2; row.incident_hold = false; row.funded = true;
   });
}

void system_contract::finalizeepoch(uint64_t epoch_id) {
   gas_settlements settlements(get_self(), get_self().value);
   auto settlement = settlements.require_find(epoch_id, "settlement not started");
   check(settlement->phase == 3 && !settlement->finalized, "settlement allocation incomplete");
   check(settlement->active_allocated == settlement->active_pool &&
         settlement->standby_allocated == settlement->standby_pool, "allocation total mismatch");
   settlements.modify(settlement, eosio::same_payer, [&](auto& row) { row.finalized = true; });
}

void system_contract::advance_gas_settlement(uint32_t current_slot) {
   gas_reward_config_singleton config_table(get_self(), get_self().value);
   if (!config_table.exists()) return;
   const auto config = config_table.get();
   if (!config.enabled || !config.first_reward_epoch.has_value()) return;

   gas_auto_state_singleton auto_state_table(get_self(), get_self().value);
   auto auto_state = auto_state_table.exists() ? auto_state_table.get() : gas_auto_state{};
   if (!auto_state.initialized) {
      auto_state.next_epoch = config.first_reward_epoch.value();
      auto_state.initialized = true;
      auto_state_table.set(auto_state, get_self());
   }

   const uint64_t current_epoch = uint64_t(current_slot) / config.epoch_slots;
   if (auto_state.next_epoch >= current_epoch) return;

   gas_settlements settlements(get_self(), get_self().value);
   auto settlement = settlements.find(auto_state.next_epoch);
   if (settlement == settlements.end()) {
      beginsettle(auto_state.next_epoch);
      return;
   }
   if (settlement->finalized) {
      ++auto_state.next_epoch;
      auto_state_table.set(auto_state, get_self());
      return;
   }
   if (settlement->phase == 4 && settlement->incident_hold) {
      // A network-wide incident must not stall all later epochs. Proceeding
      // preserves service rewards while the performance path separately avoids
      // treating the incident as an individual producer offence.
      resolveincdt(auto_state.next_epoch, true);
      return;
   }
   if (settlement->phase == 1 || settlement->phase == 2) {
      settlenext(auto_state.next_epoch, max_settle_rows);
      return;
   }
   if (settlement->phase == 3) {
      finalizeepoch(auto_state.next_epoch);
   }
}

} // namespace eosiosystem

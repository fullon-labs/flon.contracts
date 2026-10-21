#include <gas.reward/gas.reward.hpp>
#include <flon.token/flon.token.hpp>

#include <limits>

namespace flon {

gas_reward::global_state gas_reward::get_global() const {
   global_singleton global(get_self(), get_self().value);
   eosio::check(global.exists(), "gas.reward is not initialized");
   return global.get();
}

void gas_reward::init(symbol core_symbol) {
   require_auth(get_self());
   eosio::check(core_symbol.is_valid(), "invalid core symbol");
   global_singleton global(get_self(), get_self().value);
   eosio::check(!global.exists(), "already initialized");
   const asset zero(0, core_symbol);
   global.set({core_symbol, zero, zero, zero}, get_self());
}

void gas_reward::openepoch(uint64_t epoch_id, asset expected) {
   require_auth(system_account);
   auto state = get_global();
   eosio::check(expected.symbol == state.core_symbol && expected.amount >= 0, "invalid expected amount");
   reward_epochs epochs(get_self(), get_self().value);
   eosio::check(epochs.find(epoch_id) == epochs.end(), "epoch already exists");
   epochs.emplace(get_self(), [&](auto& row) {
      row.epoch_id = epoch_id;
      row.expected = expected;
      row.funded = asset(0, expected.symbol);
      row.allocated = asset(0, expected.symbol);
   });
}

void gas_reward::ontransfer(name from, name to, asset quantity, std::string memo) {
   if (to != get_self()) return;
   eosio::check(get_first_receiver() == token_account, "unsupported token contract");
   eosio::check(from == "flon.gas"_n, "only flon.gas may fund rewards");
   auto state = get_global();
   eosio::check(quantity.symbol == state.core_symbol && quantity.amount > 0, "invalid funding amount");
   constexpr const char prefix[] = "gas-reward:";
   eosio::check(memo.compare(0, sizeof(prefix) - 1, prefix) == 0, "invalid funding memo");
   uint64_t epoch_id = 0;
   eosio::check(memo.size() > sizeof(prefix) - 1, "missing funding epoch");
   for (size_t i = sizeof(prefix) - 1; i < memo.size(); ++i) {
      eosio::check(memo[i] >= '0' && memo[i] <= '9', "invalid funding epoch");
      const uint64_t digit = memo[i] - '0';
      eosio::check(epoch_id <= (std::numeric_limits<uint64_t>::max() - digit) / 10,
                   "funding epoch overflow");
      epoch_id = epoch_id * 10 + digit;
   }
   reward_epochs epochs(get_self(), get_self().value);
   auto epoch = epochs.require_find(epoch_id, "epoch not opened");
   eosio::check(!epoch->finalized, "epoch already finalized");
   eosio::check(quantity.amount <= epoch->expected.amount - epoch->funded.amount,
                "epoch overfunded");
   epochs.modify(epoch, eosio::same_payer, [&](auto& row) { row.funded += quantity; });
   state.total_funded += quantity;
   global_singleton(get_self(), get_self().value).set(state, get_self());
}

void gas_reward::creditbatch(uint64_t epoch_id, const std::vector<name>& owners,
                             const std::vector<asset>& quantities) {
   require_auth(system_account);
   eosio::check(!owners.empty() && owners.size() == quantities.size() && owners.size() <= 10,
                "batch must contain 1 to 10 matching entries");
   auto state = get_global();
   reward_epochs epochs(get_self(), get_self().value);
   auto epoch = epochs.require_find(epoch_id, "epoch not opened");
   eosio::check(!epoch->finalized, "epoch already finalized");
   epoch_credits credits(get_self(), epoch_id);
   node_rewards rewards(get_self(), get_self().value);
   auto hash = epoch->allocation_hash;
   int64_t batch_total = 0;
   name previous = epoch->last_credited;
   for (size_t i = 0; i < owners.size(); ++i) {
      eosio::check(owners[i].value > previous.value, "owners must be globally sorted and unique");
      eosio::check(quantities[i].symbol == state.core_symbol && quantities[i].amount > 0,
                   "invalid credit quantity");
      eosio::check(credits.find(owners[i].value) == credits.end(), "duplicate epoch credit");
      credits.emplace(get_self(), [&](auto& row) { row.owner = owners[i]; row.quantity = quantities[i]; });
      auto reward = rewards.find(owners[i].value);
      if (reward == rewards.end()) {
         rewards.emplace(get_self(), [&](auto& row) {
            row.owner = owners[i]; row.allocated = quantities[i];
            row.claimed = asset(0, state.core_symbol); row.unclaimed = quantities[i];
         });
      } else {
         rewards.modify(reward, eosio::same_payer, [&](auto& row) {
            row.allocated += quantities[i]; row.unclaimed += quantities[i];
         });
      }
      const auto packed = eosio::pack(std::make_tuple(hash, owners[i], quantities[i]));
      hash = eosio::sha256(packed.data(), packed.size());
      eosio::check(quantities[i].amount <= std::numeric_limits<int64_t>::max() - batch_total,
                   "credit batch total overflow");
      batch_total += quantities[i].amount;
      previous = owners[i];
   }
   eosio::check(batch_total <= epoch->funded.amount - epoch->allocated.amount,
                "credits exceed funding");
   epochs.modify(epoch, eosio::same_payer, [&](auto& row) {
      row.allocated.amount += batch_total; row.credited += owners.size();
      row.last_credited = previous; row.allocation_hash = hash;
   });
   eosio::check(batch_total <= std::numeric_limits<int64_t>::max() - state.total_allocated.amount,
                "global allocation total overflow");
   state.total_allocated.amount += batch_total;
   global_singleton(get_self(), get_self().value).set(state, get_self());
}

void gas_reward::finalize(uint64_t epoch_id, checksum256 allocation_hash) {
   require_auth(system_account);
   reward_epochs epochs(get_self(), get_self().value);
   auto epoch = epochs.require_find(epoch_id, "epoch not opened");
   eosio::check(!epoch->finalized, "epoch already finalized");
   eosio::check(epoch->funded == epoch->expected, "epoch funding incomplete");
   eosio::check(epoch->allocated == epoch->expected, "epoch allocation incomplete");
   eosio::check(epoch->allocation_hash == allocation_hash, "allocation hash mismatch");
   epochs.modify(epoch, eosio::same_payer, [&](auto& row) { row.finalized = true; });
}

void gas_reward::claimgas(name owner) {
   require_auth(owner);
   auto state = get_global();
   node_rewards rewards(get_self(), get_self().value);
   auto reward = rewards.require_find(owner.value, "node reward not found");
   eosio::check(reward->unclaimed.amount > 0, "no gas reward to claim");
   const asset quantity = reward->unclaimed;
   rewards.modify(reward, eosio::same_payer, [&](auto& row) {
      row.claimed += quantity; row.unclaimed.amount = 0;
   });
   state.total_claimed += quantity;
   global_singleton(get_self(), get_self().value).set(state, get_self());
   eosio::token::transfer_action(token_account, {{get_self(), "active"_n}})
      .send(get_self(), owner, quantity, "gas node reward");
}

} // namespace flon

#include <dao.treasury/dao.treasury.hpp>
#include <flon.token/flon.token.hpp>

namespace flon {

dao_treasury::global_state dao_treasury::require_global() const {
   global_singleton global(get_self(), get_self().value);
   eosio::check(global.exists(), "dao.treasury is not initialized");
   return global.get();
}

uint128_t dao_treasury::asset_key(name token_contract, symbol token_symbol) {
   return (static_cast<uint128_t>(token_contract.value) << 64) | token_symbol.raw();
}

void dao_treasury::init(name governance) {
   require_auth(get_self());
   eosio::check(eosio::is_account(governance), "governance account does not exist");
   global_singleton global(get_self(), get_self().value);
   eosio::check(!global.exists(), "already initialized");
   global.set({governance, 0, 0}, get_self());
}

void dao_treasury::setasset(name token_contract, symbol token_symbol, bool enabled) {
   auto state = require_global();
   require_auth(state.governance);
   eosio::check(eosio::is_account(token_contract), "token contract does not exist");
   eosio::check(token_symbol.is_valid(), "invalid token symbol");
   assets_table assets(get_self(), get_self().value);
   auto by_asset = assets.get_index<"byasset"_n>();
   auto row = by_asset.find(asset_key(token_contract, token_symbol));
   if (row == by_asset.end()) {
      eosio::check(enabled, "treasury asset not found");
      const uint64_t id = ++state.last_asset_id;
      const asset zero(0, token_symbol);
      assets.emplace(get_self(), [&](auto& value) {
         value = {id, token_contract, token_symbol, true, zero, zero, zero, zero};
      });
      global_singleton(get_self(), get_self().value).set(state, get_self());
   } else {
      by_asset.modify(row, eosio::same_payer,
                      [&](auto& value) { value.enabled = enabled; });
   }
}

void dao_treasury::ontransfer(name from, name to, asset quantity, string) {
   if (to != get_self() || from == get_self()) return;
   require_global();
   eosio::check(quantity.is_valid() && quantity.amount > 0, "invalid treasury deposit");
   assets_table assets(get_self(), get_self().value);
   auto by_asset = assets.get_index<"byasset"_n>();
   auto row = by_asset.find(asset_key(get_first_receiver(), quantity.symbol));
   eosio::check(row != by_asset.end() && row->enabled,
                "token is not enabled for treasury deposits");
   by_asset.modify(row, eosio::same_payer, [&](auto& value) {
      value.liquid += quantity;
      value.total_received += quantity;
   });
}

void dao_treasury::reserve(uint64_t proposal_id, name recipient,
                           name token_contract, asset quantity,
                           uint16_t max_payments, time_point_sec expires_at,
                           checksum256 memo_hash) {
   auto state = require_global();
   require_auth(state.governance);
   eosio::check(proposal_id > 0, "proposal ID must be positive");
   eosio::check(eosio::is_account(recipient), "recipient account does not exist");
   eosio::check(quantity.is_valid() && quantity.amount > 0, "invalid grant quantity");
   eosio::check(max_payments > 0 && max_payments <= 100, "invalid payment limit");
   eosio::check(expires_at > time_point_sec(eosio::current_time_point()),
                "grant expiry must be in the future");
   eosio::check(memo_hash != checksum256{}, "grant memo hash is required");

   assets_table assets(get_self(), get_self().value);
   auto by_asset = assets.get_index<"byasset"_n>();
   auto treasury_asset = by_asset.find(asset_key(token_contract, quantity.symbol));
   eosio::check(treasury_asset != by_asset.end() && treasury_asset->enabled,
                "treasury asset is not enabled");
   eosio::check(quantity.amount <=
                treasury_asset->liquid.amount - treasury_asset->reserved.amount,
                "insufficient unreserved treasury balance");

   const uint64_t grant_id = ++state.last_grant_id;
   grants_table grants(get_self(), get_self().value);
   grants.emplace(get_self(), [&](auto& row) {
      row.grant_id = grant_id;
      row.proposal_id = proposal_id;
      row.recipient = recipient;
      row.token_contract = token_contract;
      row.total = quantity;
      row.paid = asset(0, quantity.symbol);
      row.released = asset(0, quantity.symbol);
      row.max_payments = max_payments;
      row.created_at = time_point_sec(eosio::current_time_point());
      row.expires_at = expires_at;
      row.memo_hash = memo_hash;
   });
   by_asset.modify(treasury_asset, eosio::same_payer,
                   [&](auto& row) { row.reserved += quantity; });
   global_singleton(get_self(), get_self().value).set(state, get_self());
}

void dao_treasury::pay(uint64_t grant_id, asset quantity, string memo) {
   auto state = require_global();
   require_auth(state.governance);
   eosio::check(memo.size() <= 256, "payment memo is too long");
   grants_table grants(get_self(), get_self().value);
   auto grant = grants.require_find(grant_id, "grant not found");
   eosio::check(grant->status == grant_open, "grant is not open");
   eosio::check(time_point_sec(eosio::current_time_point()) <= grant->expires_at,
                "grant has expired");
   eosio::check(quantity.symbol == grant->total.symbol && quantity.amount > 0,
                "invalid payment quantity");
   eosio::check(grant->payment_count < grant->max_payments,
                "grant payment limit reached");
   const int64_t remaining = grant->total.amount - grant->paid.amount -
                             grant->released.amount;
   eosio::check(quantity.amount <= remaining, "payment exceeds remaining grant");

   assets_table assets(get_self(), get_self().value);
   auto by_asset = assets.get_index<"byasset"_n>();
   auto treasury_asset = by_asset.find(asset_key(grant->token_contract, quantity.symbol));
   eosio::check(treasury_asset != by_asset.end(), "treasury asset not found");
   eosio::check(quantity <= treasury_asset->liquid &&
                quantity <= treasury_asset->reserved,
                "treasury accounting balance is insufficient");

   const name recipient = grant->recipient;
   const name token_contract = grant->token_contract;
   const bool completes = quantity.amount == remaining;
   const uint64_t payment_id = static_cast<uint64_t>(grant->payment_count) + 1;
   grants.modify(grant, eosio::same_payer, [&](auto& row) {
      row.paid += quantity;
      ++row.payment_count;
      if (completes) row.status = grant_completed;
   });
   by_asset.modify(treasury_asset, eosio::same_payer, [&](auto& row) {
      row.liquid -= quantity;
      row.reserved -= quantity;
      row.total_paid += quantity;
   });
   payments_table payments(get_self(), grant_id);
   payments.emplace(get_self(), [&](auto& row) {
      row = {payment_id, quantity, memo,
             time_point_sec(eosio::current_time_point())};
   });
   eosio::token::transfer_action(token_contract, {{get_self(), "active"_n}})
      .send(get_self(), recipient, quantity, memo);
}

void dao_treasury::release(uint64_t grant_id, string reason) {
   auto state = require_global();
   require_auth(state.governance);
   eosio::check(!reason.empty() && reason.size() <= 256, "invalid release reason");
   grants_table grants(get_self(), get_self().value);
   auto grant = grants.require_find(grant_id, "grant not found");
   eosio::check(grant->status == grant_open, "grant is not open");
   const asset remaining = grant->total - grant->paid - grant->released;
   eosio::check(remaining.amount > 0, "grant has no remaining reservation");
   assets_table assets(get_self(), get_self().value);
   auto by_asset = assets.get_index<"byasset"_n>();
   auto treasury_asset = by_asset.find(asset_key(grant->token_contract,
                                                  grant->total.symbol));
   eosio::check(treasury_asset != by_asset.end() && remaining <= treasury_asset->reserved,
                "treasury reserved balance is insufficient");
   by_asset.modify(treasury_asset, eosio::same_payer,
                   [&](auto& row) { row.reserved -= remaining; });
   grants.modify(grant, eosio::same_payer, [&](auto& row) {
      row.released += remaining;
      row.close_reason = reason;
      row.status = grant_released;
   });
}

void dao_treasury::closegrant(uint64_t grant_id) {
   grants_table grants(get_self(), get_self().value);
   auto grant = grants.require_find(grant_id, "grant not found");
   eosio::check(grant->status == grant_open, "grant is not open");
   eosio::check(time_point_sec(eosio::current_time_point()) > grant->expires_at,
                "grant has not expired");
   const asset remaining = grant->total - grant->paid - grant->released;
   assets_table assets(get_self(), get_self().value);
   auto by_asset = assets.get_index<"byasset"_n>();
   auto treasury_asset = by_asset.find(asset_key(grant->token_contract,
                                                  grant->total.symbol));
   eosio::check(treasury_asset != by_asset.end() && remaining <= treasury_asset->reserved,
                "treasury reserved balance is insufficient");
   if (remaining.amount > 0) {
      by_asset.modify(treasury_asset, eosio::same_payer,
                      [&](auto& row) { row.reserved -= remaining; });
   }
   grants.modify(grant, eosio::same_payer, [&](auto& row) {
      row.released += remaining;
      row.close_reason = "expired";
      row.status = grant_expired;
   });
}

} // namespace flon

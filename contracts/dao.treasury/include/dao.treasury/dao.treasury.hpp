#pragma once

#include <eosio/asset.hpp>
#include <eosio/crypto.hpp>
#include <eosio/eosio.hpp>
#include <eosio/singleton.hpp>
#include <eosio/time.hpp>

#include <string>

namespace flon {

using eosio::asset;
using eosio::checksum256;
using eosio::name;
using eosio::symbol;
using eosio::time_point_sec;
using std::string;

class [[eosio::contract("dao.treasury")]] dao_treasury : public eosio::contract {
 public:
   using contract::contract;

   enum grant_status : uint8_t {
      grant_open = 0,
      grant_completed = 1,
      grant_released = 2,
      grant_expired = 3
   };

   [[eosio::action]] void init(name governance);
   [[eosio::action]] void setasset(name token_contract, symbol token_symbol,
                                   bool enabled);
   [[eosio::action]] void reserve(uint64_t proposal_id, name recipient,
                                  name token_contract, asset quantity,
                                  uint16_t max_payments,
                                  time_point_sec expires_at,
                                  checksum256 memo_hash);
   [[eosio::action]] void pay(uint64_t grant_id, asset quantity, string memo);
   [[eosio::action]] void release(uint64_t grant_id, string reason);
   [[eosio::action]] void closegrant(uint64_t grant_id);

   [[eosio::on_notify("*::transfer")]]
   void ontransfer(name from, name to, asset quantity, string memo);

 private:
   struct [[eosio::table("global")]] global_state {
      name governance;
      uint64_t last_asset_id = 0;
      uint64_t last_grant_id = 0;
   };
   using global_singleton = eosio::singleton<"global"_n, global_state>;

   struct [[eosio::table]] treasury_asset {
      uint64_t id;
      name token_contract;
      symbol token_symbol;
      bool enabled = false;
      asset liquid;
      asset reserved;
      asset total_received;
      asset total_paid;
      uint64_t primary_key() const { return id; }
      uint128_t by_asset() const {
         return (static_cast<uint128_t>(token_contract.value) << 64) |
                token_symbol.raw();
      }
   };
   using assets_table = eosio::multi_index<"assets"_n, treasury_asset,
      eosio::indexed_by<"byasset"_n,
         eosio::const_mem_fun<treasury_asset, uint128_t, &treasury_asset::by_asset>>>;

   struct [[eosio::table]] grant {
      uint64_t grant_id;
      uint64_t proposal_id;
      name recipient;
      name token_contract;
      asset total;
      asset paid;
      asset released;
      uint16_t max_payments = 1;
      uint16_t payment_count = 0;
      time_point_sec created_at;
      time_point_sec expires_at;
      checksum256 memo_hash;
      string close_reason;
      uint8_t status = grant_open;
      uint64_t primary_key() const { return grant_id; }
      uint64_t by_proposal() const { return proposal_id; }
   };
   using grants_table = eosio::multi_index<"grants"_n, grant,
      eosio::indexed_by<"byproposal"_n,
         eosio::const_mem_fun<grant, uint64_t, &grant::by_proposal>>>;

   struct [[eosio::table]] payment_record {
      uint64_t payment_id;
      asset quantity;
      string memo;
      time_point_sec paid_at;
      uint64_t primary_key() const { return payment_id; }
   };
   using payments_table = eosio::multi_index<"payments"_n, payment_record>;

   global_state require_global() const;
   static uint128_t asset_key(name token_contract, symbol token_symbol);
};

} // namespace flon

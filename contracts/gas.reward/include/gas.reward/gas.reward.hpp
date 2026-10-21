#pragma once

#include <eosio/asset.hpp>
#include <eosio/crypto.hpp>
#include <eosio/eosio.hpp>
#include <eosio/singleton.hpp>

namespace flon {

using eosio::asset;
using eosio::checksum256;
using eosio::name;
using eosio::symbol;

class [[eosio::contract("gas.reward")]] gas_reward : public eosio::contract {
 public:
   using contract::contract;

   [[eosio::action]] void init(symbol core_symbol);
   [[eosio::action]] void openepoch(uint64_t epoch_id, asset expected);
   [[eosio::action]] void creditbatch(uint64_t epoch_id,
      const std::vector<name>& owners, const std::vector<asset>& quantities);
   [[eosio::action]] void finalize(uint64_t epoch_id, checksum256 allocation_hash);
   [[eosio::action]] void claimgas(name owner);

   [[eosio::on_notify("flon.token::transfer")]]
   void ontransfer(name from, name to, asset quantity, std::string memo);

   using openepoch_action = eosio::action_wrapper<"openepoch"_n, &gas_reward::openepoch>;
   using creditbatch_action = eosio::action_wrapper<"creditbatch"_n, &gas_reward::creditbatch>;
   using finalize_action = eosio::action_wrapper<"finalize"_n, &gas_reward::finalize>;

 private:
   static constexpr name system_account = "flon"_n;
   static constexpr name token_account = "flon.token"_n;

   struct [[eosio::table("global")]] global_state {
      symbol core_symbol;
      asset total_funded;
      asset total_allocated;
      asset total_claimed;
   };
   using global_singleton = eosio::singleton<"global"_n, global_state>;

   struct [[eosio::table]] reward_epoch {
      uint64_t epoch_id;
      asset expected;
      asset funded;
      asset allocated;
      uint32_t credited = 0;
      name last_credited;
      checksum256 allocation_hash;
      bool finalized = false;
      uint64_t primary_key() const { return epoch_id; }
   };
   using reward_epochs = eosio::multi_index<"rewardepochs"_n, reward_epoch>;

   struct [[eosio::table]] node_reward {
      name owner;
      asset allocated;
      asset claimed;
      asset unclaimed;
      uint64_t primary_key() const { return owner.value; }
   };
   using node_rewards = eosio::multi_index<"noderewards"_n, node_reward>;

   struct [[eosio::table]] epoch_credit {
      name owner;
      asset quantity;
      uint64_t primary_key() const { return owner.value; }
   };
   using epoch_credits = eosio::multi_index<"epochcredits"_n, epoch_credit>;

   global_state get_global() const;
};

} // namespace flon

#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <eosio/singleton.hpp>
#include <eosio/privileged.hpp>
#include <contract_version.hpp>

#include <string>

#define PP(prop) "," #prop ":", prop
#define PP0(prop) #prop ":", prop
#define PRINT_PROPERTIES(...) eosio::print("{", __VA_ARGS__, "}")

#define CHECK(exp, msg) { if (!(exp)) eosio::check(false, msg); }

#ifndef ASSERT
    #define ASSERT(exp) CHECK(exp, #exp)
#endif

namespace flon {

   using std::string;
   using eosio::contract;
   using eosio::name;
   using eosio::asset;
   using eosio::symbol;
   using eosio::block_timestamp;

   static constexpr name      SYSTEM_CONTRACT   = "flon"_n;
   static constexpr name      CORE_TOKEN        = "flon.token"_n;
   // static constexpr symbol    vote_symbol       = symbol("VOTE", 4);
   // static const asset         vote_asset_0      = asset(0, vote_symbol);
   static constexpr int128_t  HIGH_PRECISION    = 1'000'000'000'000'000'000; // 10^18

   /**
    * The `flon.reward` contract is used as a reward dispatcher contract for flon.system contract.
    *
    */
   class [[eosio::contract("flon.reward")]] flon_reward : public contract {
      public:
         flon_reward( name s, name code, eosio::datastream<const char*> ds ):
               contract(s, code, ds),
               _global(get_self(), get_self().value),
               _voter_tbl(get_self(), get_self().value),
               _producer_tbl(get_self(), get_self().value)
         {
            _gstate  = _global.exists() ? _global.get() : global_state{};
         }

         DEFINE_VERSION_ACTION(flon_reward)

         /**
          * Register producer action.
          * Producer must register before add rewards.
          * Producer must pay for storage.
          *
          * @param producer - account registering to be a producer candidate,
          *
          */
         [[eosio::action]]
         void init( const symbol& core_symbol );

         /**
          * Register producer action.
          * Producer must register before add rewards.
          * Producer must pay for storage.
          *
          * @param producer - account registering to be a producer candidate,
          *
          */
         [[eosio::action]]
         void regproducer( const name& producer );

         /**
          * addvote.
          *
          * @param voter      - the account of voter,
          * @param votes      - votes value,
          */
         [[eosio::action]]
         void addvote( const name& voter, const int64_t votes );

         /**
          * subvote.
          *
          * @param voter      - the account of voter,
          * @param votes      - votes value,
          */
         [[eosio::action]]
         void subvote( const name& voter, const int64_t votes );


         /**
          * Vote producer action, votes for a set of producers.
          *
          * @param voter - the account to change the voted producers for,
          * @param producers - the list of producers to vote for, a maximum of 30 producers is allowed.
          */
         [[eosio::action]]
         void voteproducer( const name& voter, const std::vector<name>& producers );

         /**
          * claim rewards for voter
          *
          * @param voter - the account of voter
          */
         [[eosio::action]]
         void claimrewards( const name& voter );

         /**
          * claim rewards for voter
          *
          * @param claimer - the account of claimer
          * @param voter - the account of voter
          */
         ACTION claimfor(const name& clamer, const name& voter );
        /**
         * Notify by transfer() of xtoken contract
         *
         * @param from - the account to transfer from,
         * @param to - the account to be transferred to,
         * @param quantity - the quantity of tokens to be transferred,
         * @param memo - the memo string to accompany the transaction.
         */
        [[eosio::on_notify("flon.token::transfer")]]
        void ontransfer(   const name &from,
                           const name &to,
                           const asset &quantity,
                           const string &memo);

         using init_action = eosio::action_wrapper<"init"_n, &flon_reward::init>;
         using regproducer_action = eosio::action_wrapper<"regproducer"_n, &flon_reward::regproducer>;
         using addvote_action = eosio::action_wrapper<"addvote"_n, &flon_reward::addvote>;
         using subvote_action = eosio::action_wrapper<"subvote"_n, &flon_reward::subvote>;
         using voteproducer_action = eosio::action_wrapper<"voteproducer"_n, &flon_reward::voteproducer>;
         using claimrewards_action = eosio::action_wrapper<"claimrewards"_n, &flon_reward::claimrewards>;
         using claimfor_action = eosio::action_wrapper<"claimfor"_n, &flon_reward::claimfor>;
   public:
         struct [[eosio::table("global")]] global_state {
            asset                total_rewards;

            typedef eosio::singleton< "global"_n, global_state >   table;
         };

         /**
          * producer table.
          * scope: contract self
         */
         struct [[eosio::table]] producer {
            name              owner;                                 // PK
            bool              is_registered        = false;          // is initialized
            asset             total_rewards;
            asset             allocating_rewards;
            asset             allocated_rewards; // = total_rewards - allocating_rewards
            int64_t           votes                = 0;
            int128_t          rewards_per_vote     = 0;
            block_timestamp   update_at;

            uint64_t primary_key()const { return owner.value; }

            typedef eosio::multi_index< "producers"_n, producer > table;
         };

         struct voted_producer_info {
            int128_t           last_rewards_per_vote         = 0;
         };

         using voted_producer_map = std::map<name, voted_producer_info>;

         /**
          * voter table.
          * scope: contract self
         */
         struct [[eosio::table]] voter {
            name                       owner;
            int64_t                    votes;
            voted_producer_map         producers;
            asset                      unclaimed_rewards;
            asset                      claimed_rewards;
            block_timestamp            update_at;

            uint64_t primary_key()const { return owner.value; }

            typedef eosio::multi_index< "voters"_n, voter > table;
         };


         static inline bool is_producer_registered(const name& contract_account, const name& producer) {
            producer::table producer_tbl(contract_account, contract_account.value);
            auto itr = producer_tbl.find(producer.value);
            if (itr != producer_tbl.end()) {
               return itr->is_registered;
            }
            return false;
         }
   private:
      global_state::table     _global;
      global_state            _gstate;
      voter::table            _voter_tbl;
      producer::table         _producer_tbl;

      void claim_rewards( const name& voter );
      void allocate_producer_rewards(voted_producer_map& producers, int64_t votes_old, int64_t votes_delta, const name& new_payer, asset &allocated_rewards_out);
      void change_vote(const name& voter, int64_t votes, bool is_adding);
      void check_init() const;
      const symbol& core_symbol() const;
      asset calc_voter_rewards(int64_t votes, const int128_t& rewards_per_vote) const;
   };

}

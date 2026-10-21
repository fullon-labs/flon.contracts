#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <eosio/singleton.hpp>
#include <eosio/privileged.hpp>

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
    * The `voter.reward` contract distributes producer-shared rewards to voters.
    *
    */
   class [[eosio::contract("voter.reward")]] voter_reward : public contract {
      public:
         voter_reward( name s, name code, eosio::datastream<const char*> ds ):
               contract(s, code, ds),
               _global(get_self(), get_self().value),
               _voter_tbl(get_self(), get_self().value),
               _producer_tbl(get_self(), get_self().value)
         {
            _gstate  = _global.exists() ? _global.get() : global_state{};
         }

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
         void addvote( const name& voter, const int64_t votes, uint64_t activation_epoch );

         /**
          * subvote.
          *
          * @param voter      - the account of voter,
          * @param votes      - votes value,
          */
         [[eosio::action]]
         void subvote( const name& voter, const int64_t votes, uint64_t activation_epoch );


         /**
          * Vote producer action, votes for a set of producers.
          *
          * @param voter - the account to change the voted producers for,
          * @param producers - the list of producers to vote for, a maximum of 30 producers is allowed.
          * The voter's aggregate reward weight is divided equally among the selected producers.
          */
         [[eosio::action]]
         void voteproducer( const name& voter, const std::vector<name>& producers,
                            uint64_t activation_epoch );

         /**
          * Legacy self-claim entrypoint. Third-party claims are not allowed.
          *
          * @param voter - the account of voter
          */
         [[eosio::action]]
         void claimrewards( const name& voter );

         /**
          * Seal a completed reward epoch after all producer reward transfers
          * for that epoch have been received. Only the system contract may
          * advance this monotonically increasing boundary.
          */
         [[eosio::action]]
         void closeepoch( uint64_t epoch_id );

         /**
          * claim rewards for voter
          *
          * @param claimer - must equal voter
          * @param voter - the account of voter
          */
         ACTION claimfor(const name& claimer, const name& voter );
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

         using init_action = eosio::action_wrapper<"init"_n, &voter_reward::init>;
         using regproducer_action = eosio::action_wrapper<"regproducer"_n, &voter_reward::regproducer>;
         using addvote_action = eosio::action_wrapper<"addvote"_n, &voter_reward::addvote>;
         using subvote_action = eosio::action_wrapper<"subvote"_n, &voter_reward::subvote>;
         using voteproducer_action = eosio::action_wrapper<"voteproducer"_n, &voter_reward::voteproducer>;
         using claimrewards_action = eosio::action_wrapper<"claimrewards"_n, &voter_reward::claimrewards>;
         using closeepoch_action = eosio::action_wrapper<"closeepoch"_n, &voter_reward::closeepoch>;
         using claimfor_action = eosio::action_wrapper<"claimfor"_n, &voter_reward::claimfor>;
   public:
         struct [[eosio::table("global")]] global_state {
            asset                total_rewards;

            typedef eosio::singleton< "global"_n, global_state >   table;
         };

         struct [[eosio::table("epochstate")]] epoch_state {
            uint64_t next_epoch_to_close = 0;

            typedef eosio::singleton<"epochstate"_n, epoch_state> table;
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
            int64_t                    votes = 0;
            voted_producer_map         producers;
            asset                      unclaimed_rewards;
            asset                      claimed_rewards;
            block_timestamp            update_at;

            uint64_t primary_key()const { return owner.value; }

            typedef eosio::multi_index< "voters"_n, voter > table;
         };

         struct [[eosio::table]] pending_vote {
            name                       owner;
            int64_t                    votes = 0;
            std::vector<name>          producers;
            uint64_t                   activation_epoch = 0;

            uint64_t primary_key() const { return owner.value; }
            typedef eosio::multi_index<"pendingvote"_n, pending_vote> table;
         };

         struct [[eosio::table]] epoch_index {
            name     producer;
            int128_t rewards_per_vote = 0;

            uint64_t primary_key() const { return producer.value; }
            typedef eosio::multi_index<"epochindex"_n, epoch_index> table;
         };

         struct [[eosio::table]] producer_delta {
            name    producer;
            int64_t votes_delta = 0;

            uint64_t primary_key() const { return producer.value; }
            typedef eosio::multi_index<"votedeltas"_n, producer_delta> table;
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
      epoch_state::table      _epoch_state_tbl{get_self(), get_self().value};
      voter::table            _voter_tbl;
      producer::table         _producer_tbl;
      pending_vote::table     _pending_vote_tbl{get_self(), get_self().value};
      producer_delta::table   _producer_delta_tbl{get_self(), get_self().value};

      void claim_rewards( const name& voter );
      void allocate_producer_rewards(voted_producer_map& producers, int64_t weight_old, int64_t weight_delta, const name& new_payer, asset &allocated_rewards_out);
      void change_vote(const name& voter, int64_t votes, bool is_adding, uint64_t activation_epoch);
      void materialize_pending(const name& voter);
      void queue_vote_change(const name& voter, int64_t votes,
                             const std::vector<name>& producers, uint64_t activation_epoch);
      void update_pending_totals(const std::vector<name>& old_producers, int64_t old_votes,
                                 const std::vector<name>& new_producers, int64_t new_votes);
      void check_init() const;
      const symbol& core_symbol() const;
      asset calc_voter_rewards(int64_t votes, const int128_t& rewards_per_vote) const;
   };

}

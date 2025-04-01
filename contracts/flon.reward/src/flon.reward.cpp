#include <flon.reward/flon.reward.hpp>
#include <eosio/system.hpp>
#ifdef ENABLE_CONTRACT_VERSION
#include <contract_version.hpp>
#endif//ENABLE_CONTRACT_VERSION


namespace flon {

#ifdef ENABLE_CONTRACT_VERSION
DEFINE_VERSION_CONTRACT_CLASS("flon.reward", flon_reward)
#endif//ENABLE_CONTRACT_VERSION

using namespace eosio;

static constexpr name ACTIVE_PERM       = "active"_n;

struct flon_token {
      void transfer( const name&    from,
                     const name&    to,
                     const asset&   quantity,
                     const string&  memo );
      using transfer_action = eosio::action_wrapper<"transfer"_n, &flon_token::transfer>;
};

#define TRANSFER_OUT(token_contract, to, quantity, memo)                             \
            flon_token::transfer_action(token_contract, {{get_self(), ACTIVE_PERM}}) \
               .send(get_self(), to, quantity, memo);


namespace db {

    template<typename table, typename Lambda>
    inline void set(table &tbl,  typename table::const_iterator& itr, const eosio::name& emplaced_payer,
            const eosio::name& modified_payer, Lambda&& setter )
   {
        if (itr == tbl.end()) {
            tbl.emplace(emplaced_payer, [&]( auto& p ) {
               setter(p, true);
            });
        } else {
            tbl.modify(itr, modified_payer, [&]( auto& p ) {
               setter(p, false);
            });
        }
    }

    template<typename table, typename Lambda>
    inline void set(table &tbl,  typename table::const_iterator& itr, const eosio::name& emplaced_payer,
               Lambda&& setter )
   {
      set(tbl, itr, emplaced_payer, eosio::same_payer, setter);
   }

}// namespace db

inline static int128_t calc_rewards_per_vote(const int128_t& old_rewards_per_vote, const asset& rewards, int64_t votes) {
   ASSERT(rewards.amount >= 0 && votes >= 0);
   int128_t  new_rewards_per_vote = old_rewards_per_vote;
   if (rewards.amount > 0 && votes > 0) {
      new_rewards_per_vote = old_rewards_per_vote + rewards.amount * HIGH_PRECISION / votes;
      CHECK(new_rewards_per_vote >= old_rewards_per_vote, "calculated rewards_per_vote overflow")
   }
   return new_rewards_per_vote;
}

 asset flon_reward::calc_voter_rewards(int64_t votes, const int128_t& rewards_per_vote) const {
   ASSERT(votes >= 0 && rewards_per_vote >= 0);
   CHECK(votes * rewards_per_vote >= rewards_per_vote, "calculated rewards overflow");
   int128_t rewards = votes * rewards_per_vote / HIGH_PRECISION;
   CHECK(rewards >= 0 && rewards <= std::numeric_limits<int64_t>::max(), "calculated rewards overflow");
   return asset((int64_t)rewards, core_symbol());
}

void flon_reward::init( const symbol& core_symbol ) {
   require_auth(SYSTEM_CONTRACT);
   CHECK(!_gstate.total_rewards.symbol.is_valid(), "reward contract has already been initialized")
   _gstate.total_rewards = asset(0, core_symbol);
   _global.set(_gstate, get_self());
}

void flon_reward::regproducer( const name& producer ) {
   check_init();

   require_auth(producer);

   check(is_account(producer), "producer account not found");

   auto now = eosio::current_time_point();

   auto prod_itr = _producer_tbl.find(producer.value);
   db::set(_producer_tbl, prod_itr, producer, producer, [&]( auto& p, bool is_new ) {
      if (is_new) {
         p.owner =  producer;
         p.total_rewards = asset(0, core_symbol());
         p.allocating_rewards = asset(0, core_symbol());
         p.allocated_rewards = asset(0, core_symbol());
      }
      p.is_registered = true;
      p.update_at = now;
   });
}

void flon_reward::addvote( const name& voter, int64_t votes ) {
   check_init();
   change_vote(voter, votes, true /* is_adding */);
}


void flon_reward::subvote( const name& voter, const int64_t votes ) {
   check_init();
   change_vote(voter, votes, false /* is_adding */);
}

void flon_reward::voteproducer( const name& voter, const std::vector<name>& producers ) {
   check_init();

   require_auth( SYSTEM_CONTRACT );
   require_auth( voter );

   for( size_t i = 1; i < producers.size(); ++i ) {
      check( producers[i - 1] < producers[i], "producer votes must be uniqued and sorted" );
   }

   auto now = eosio::current_time_point();

   auto voter_itr = _voter_tbl.find(voter.value);
   db::set(_voter_tbl, voter_itr, voter, voter, [&]( auto& v, bool is_new ) {
      if (is_new) {
         v.owner = voter;
         v.unclaimed_rewards = asset(0, core_symbol());
         v.claimed_rewards = asset(0, core_symbol());
      }

      voted_producer_map added_prods;
      voted_producer_map removed_prods;

      auto new_prod_itr = producers.begin();
      auto old_prod_itr = v.producers.begin();
      while( old_prod_itr != v.producers.end() || new_prod_itr != producers.end() ) {

         if (old_prod_itr != v.producers.end() && new_prod_itr != producers.end()) {
            if ( old_prod_itr->first < (*new_prod_itr) ) {
               // old is discarded and add to removed_prods, new is processed in next loop
               removed_prods.emplace(*old_prod_itr);
               old_prod_itr = v.producers.erase(old_prod_itr);
            }else if ( (*new_prod_itr) < old_prod_itr->first ) {
               // old is processed in next loop, new is added to added_prods
               added_prods[*new_prod_itr] = {};
               new_prod_itr++;
            } else { // old_prod_itr->first == (*new_prod_itr))
               // old is keeped in v.producers, new is discarded
               old_prod_itr++;
               new_prod_itr++;
            }
         } else if (old_prod_itr != v.producers.end()) {
            // no new, old is discarded and add to removed_prods,
            removed_prods.emplace(*old_prod_itr);
            old_prod_itr = v.producers.erase(old_prod_itr);
         } else { // new_prod_itr != producers.end()
            // no old, new is added to added_prods
            added_prods[*new_prod_itr] = {};
            new_prod_itr++;
         }
      }

      allocate_producer_rewards(removed_prods, v.votes, -v.votes, voter, v.unclaimed_rewards);
      allocate_producer_rewards(v.producers, v.votes, 0, voter, v.unclaimed_rewards);
      allocate_producer_rewards(added_prods, 0, v.votes, voter, v.unclaimed_rewards);
      for (auto& added_prod : added_prods) {
         v.producers.emplace(added_prod);
      }

      v.update_at    = now;
   });
}

void flon_reward::claimrewards(const name& voter) {
   check_init();
   require_auth( voter );
   claim_rewards(voter);
}

void flon_reward::claim_rewards( const name& voter ) {
   auto voter_itr = _voter_tbl.find(voter.value);
   check(voter_itr != _voter_tbl.end(), "voter info not found");

   _voter_tbl.modify(voter_itr, voter, [&]( auto& v) {
      if (voter_itr->votes > 0) {
         allocate_producer_rewards(v.producers, v.votes, 0, voter, v.unclaimed_rewards);
      }
      check(v.unclaimed_rewards.amount > 0, "no rewards to claim");

      TRANSFER_OUT(CORE_TOKEN, voter, v.unclaimed_rewards, "voted rewards");

      v.claimed_rewards += v.unclaimed_rewards;
      v.unclaimed_rewards.amount = 0;
      v.update_at = current_time_point();
   });
}

void flon_reward::claimfor(const name& claimer, const name& voter) {
   check_init();
   require_auth( claimer );
   claim_rewards(voter);
}

void flon_reward::ontransfer(    const name &from,
                                 const name &to,
                                 const asset &quantity,
                                 const string &memo)
{
   if (get_first_receiver() == CORE_TOKEN && quantity.symbol == core_symbol() && from != get_self() && to == get_self()) {
      _gstate.total_rewards += quantity;
      _global.set(_gstate, get_self());

      auto prod_itr = _producer_tbl.find(from.value);
      check(prod_itr != _producer_tbl.end(), "producer(from) not found");
      check(prod_itr->is_registered, "producer(from) not registered");
      _producer_tbl.modify(prod_itr, same_payer, [&]( auto& p ) {
         p.total_rewards         += quantity;
         p.allocating_rewards   += quantity;
         p.rewards_per_vote      = calc_rewards_per_vote(p.rewards_per_vote, quantity, p.votes);
         p.update_at = eosio::current_time_point();
      });
   }
}

void flon_reward::change_vote(const name& voter, int64_t votes, bool is_adding) {
   require_auth( SYSTEM_CONTRACT );
   require_auth( voter );

   CHECK(votes > 0, "votes must be positive")

   auto now = eosio::current_time_point();
   auto voter_itr = _voter_tbl.find(voter.value);
   db::set(_voter_tbl, voter_itr, voter, voter, [&]( auto& v, bool is_new ) {
      if (is_new) {
         v.owner = voter;
         v.unclaimed_rewards = asset(0, core_symbol());
         v.claimed_rewards = asset(0, core_symbol());
      }

      auto votes_delta = votes;
      if (!is_adding) {
         CHECK(v.votes >= votes, "voter's votes insufficent")
         votes_delta = -votes;
      }
      allocate_producer_rewards(v.producers, v.votes, votes_delta, voter, v.unclaimed_rewards);
      v.votes        += votes_delta;
      CHECK(v.votes >= 0, "voter's votes can not be negative")

      v.update_at    = now;
   });
}

void flon_reward::allocate_producer_rewards(voted_producer_map& producers, int64_t votes_old,
         int64_t votes_delta, const name& new_payer, asset &allocated_rewards_out)
{

   auto now = eosio::current_time_point();
   for ( auto& voted_prod : producers) {
      const auto& prod_name = voted_prod.first;
      auto& last_rewards_per_vote = voted_prod.second.last_rewards_per_vote; // will be updated below

      auto prod_itr = _producer_tbl.find(prod_name.value);
      db::set(_producer_tbl, prod_itr, new_payer, same_payer, [&]( auto& p, bool is_new ) {
         if (is_new) {
            p.owner = prod_name;
            p.total_rewards = asset(0, core_symbol());
            p.allocating_rewards = asset(0, core_symbol());
            p.allocated_rewards = asset(0, core_symbol());
         }

         CHECK(p.rewards_per_vote >= last_rewards_per_vote, "last_rewards_per_vote invalid");
         int128_t rewards_per_vote_delta = p.rewards_per_vote - last_rewards_per_vote;
         if (rewards_per_vote_delta > 0 && votes_old > 0) {
            ASSERT(votes_old <= p.votes)
            asset new_rewards = calc_voter_rewards(votes_old, rewards_per_vote_delta);
            CHECK(p.allocating_rewards >= new_rewards, "producer allocating rewards insufficient");
            p.allocating_rewards -= new_rewards;
            p.allocated_rewards += new_rewards;

            ASSERT(p.total_rewards == p.allocating_rewards + p.allocated_rewards)

            allocated_rewards_out += new_rewards; // update allocated_rewards for voter
         }

         p.votes += votes_delta;
         CHECK(p.votes >= 0, "producer votes can not be negative")
         p.update_at = now;

         last_rewards_per_vote = p.rewards_per_vote; // update for voted_prod
      });
   }
}

void flon_reward::check_init() const {
   CHECK( _gstate.total_rewards.symbol.is_valid(), "reward contract has not been initialized" )
}

const symbol& flon_reward::core_symbol() const {
   return _gstate.total_rewards.symbol;
}

} /// namespace flon

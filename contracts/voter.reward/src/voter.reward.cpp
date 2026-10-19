#include <voter.reward/voter.reward.hpp>
#include <eosio/system.hpp>
#ifdef ENABLE_CONTRACT_VERSION
#include <contract_version.hpp>
#endif//ENABLE_CONTRACT_VERSION


namespace flon {

#ifdef ENABLE_CONTRACT_VERSION
DEFINE_VERSION_CONTRACT_CLASS("voter.reward", voter_reward)
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

#define TRANSFER_OUT(token_contract, to, quantity, memo)                            \
   flon_token::transfer_action(token_contract, {{get_self(), ACTIVE_PERM}})         \
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

 asset voter_reward::calc_voter_rewards(int64_t votes, const int128_t& rewards_per_vote) const {
   ASSERT(votes >= 0 && rewards_per_vote >= 0);
   CHECK(votes * rewards_per_vote >= rewards_per_vote, "calculated rewards overflow");
   int128_t rewards = votes * rewards_per_vote / HIGH_PRECISION;
   CHECK(rewards >= 0 && rewards <= std::numeric_limits<int64_t>::max(), "calculated rewards overflow");
   return asset((int64_t)rewards, core_symbol());
}

void voter_reward::init( const symbol& core_symbol ) {
   require_auth(SYSTEM_CONTRACT);
   CHECK(!_gstate.total_rewards.symbol.is_valid(), "reward contract has already been initialized")
   _gstate.total_rewards = asset(0, core_symbol);
   _global.set(_gstate, get_self());
}

void voter_reward::regproducer( const name& producer ) {
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

void voter_reward::addvote( const name& voter, int64_t votes, uint64_t activation_epoch ) {
   check_init();
   change_vote(voter, votes, true /* is_adding */, activation_epoch);
}


void voter_reward::subvote( const name& voter, const int64_t votes, uint64_t activation_epoch ) {
   check_init();
   change_vote(voter, votes, false /* is_adding */, activation_epoch);
}

void voter_reward::voteproducer( const name& voter, const std::vector<name>& producers,
                                 uint64_t activation_epoch ) {
   check_init();

   require_auth( SYSTEM_CONTRACT );
   require_auth( voter );

   for( size_t i = 1; i < producers.size(); ++i ) {
      check( producers[i - 1] < producers[i], "producer votes must be uniqued and sorted" );
   }

   materialize_pending(voter);
   auto voter_itr = _voter_tbl.find(voter.value);
   check(voter_itr != _voter_tbl.end(), "voter info not found");

   auto pending_itr = _pending_vote_tbl.find(voter.value);
   const int64_t target_votes = pending_itr == _pending_vote_tbl.end()
                                  ? voter_itr->votes : pending_itr->votes;
   queue_vote_change(voter, target_votes, producers, activation_epoch);
}

void voter_reward::claimrewards(const name& voter) {
   check_init();
   require_auth( voter );
   claim_rewards(voter);
}

void voter_reward::closeepoch(uint64_t epoch_id) {
   check_init();
   require_auth(SYSTEM_CONTRACT);

   auto state = _epoch_state_tbl.exists() ? _epoch_state_tbl.get() : epoch_state{};
   CHECK(epoch_id == state.next_epoch_to_close, "reward epochs must be closed sequentially")
   epoch_index::table checkpoint(get_self(), epoch_id + 1);
   for (auto prod_itr = _producer_tbl.begin(); prod_itr != _producer_tbl.end(); ++prod_itr) {
      checkpoint.emplace(get_self(), [&](auto& row) {
         row.producer = prod_itr->owner;
         row.rewards_per_vote = prod_itr->rewards_per_vote;
      });
   }

   for (auto delta_itr = _producer_delta_tbl.begin(); delta_itr != _producer_delta_tbl.end();) {
      auto prod_itr = _producer_tbl.find(delta_itr->producer.value);
      CHECK(prod_itr != _producer_tbl.end(), "producer not found")
      _producer_tbl.modify(prod_itr, same_payer, [&](auto& p) {
         p.votes += delta_itr->votes_delta;
         CHECK(p.votes >= 0, "producer votes can not be negative")
      });
      delta_itr = _producer_delta_tbl.erase(delta_itr);
   }

   state.next_epoch_to_close += 1;
   _epoch_state_tbl.set(state, get_self());
}

void voter_reward::claim_rewards( const name& voter ) {
   materialize_pending(voter);
   auto voter_itr = _voter_tbl.find(voter.value);
   check(voter_itr != _voter_tbl.end(), "voter info not found");

   _voter_tbl.modify(voter_itr, voter, [&]( auto& v) {
      if (voter_itr->votes > 0) {
         const int64_t allocated_weight = v.producers.empty() ? 0 : v.votes / static_cast<int64_t>(v.producers.size());
         allocate_producer_rewards(v.producers, allocated_weight, 0, voter, v.unclaimed_rewards);
      }
      check(v.unclaimed_rewards.amount > 0, "no rewards to claim");

      TRANSFER_OUT(CORE_TOKEN, voter, v.unclaimed_rewards, "voted rewards");

      v.claimed_rewards += v.unclaimed_rewards;
      v.unclaimed_rewards.amount = 0;
      v.update_at = current_time_point();
   });
}

void voter_reward::claimfor(const name& claimer, const name& voter) {
   check_init();
   require_auth( claimer );
   claim_rewards(voter);
}

void voter_reward::ontransfer(    const name &from,
                                 const name &to,
                                 const asset &quantity,
                                 const string &memo)
{
   if (get_first_receiver() == CORE_TOKEN && quantity.symbol == core_symbol() && from != get_self() && to == get_self()) {
      _gstate.total_rewards += quantity;
      _global.set(_gstate, get_self());

      // Parse producer name from memo (for rewards from flon system contract)
      // Or use 'from' if memo is empty (for direct transfers from producers)
      name producer_name;
      if (!memo.empty()) {
         producer_name = name(memo);
      } else {
         producer_name = from;
      }

      auto prod_itr = _producer_tbl.find(producer_name.value);
      check(prod_itr != _producer_tbl.end(), "producer not found");
      check(prod_itr->is_registered, "producer not registered");
      _producer_tbl.modify(prod_itr, same_payer, [&]( auto& p ) {
         p.total_rewards         += quantity;
         p.allocating_rewards   += quantity;
         p.rewards_per_vote      = calc_rewards_per_vote(p.rewards_per_vote, quantity, p.votes);
         p.update_at = eosio::current_time_point();
      });
   }
}

void voter_reward::materialize_pending(const name& voter) {
   auto pending_itr = _pending_vote_tbl.find(voter.value);
   if (pending_itr == _pending_vote_tbl.end()) return;

   auto state = _epoch_state_tbl.exists() ? _epoch_state_tbl.get() : epoch_state{};
   if (pending_itr->activation_epoch > state.next_epoch_to_close) return;

   auto voter_itr = _voter_tbl.find(voter.value);
   CHECK(voter_itr != _voter_tbl.end(), "voter info not found")
   epoch_index::table checkpoint(get_self(), pending_itr->activation_epoch);

   _voter_tbl.modify(voter_itr, same_payer, [&](auto& v) {
      const int64_t old_weight = v.producers.empty()
                                   ? 0 : v.votes / static_cast<int64_t>(v.producers.size());
      for (auto& old_position : v.producers) {
         auto checkpoint_itr = checkpoint.find(old_position.first.value);
         CHECK(checkpoint_itr != checkpoint.end(), "missing reward epoch checkpoint")
         CHECK(checkpoint_itr->rewards_per_vote >= old_position.second.last_rewards_per_vote,
               "invalid reward epoch checkpoint")
         const int128_t delta = checkpoint_itr->rewards_per_vote -
                                old_position.second.last_rewards_per_vote;
         if (old_weight > 0 && delta > 0) {
            auto reward = calc_voter_rewards(old_weight, delta);
            auto prod_itr = _producer_tbl.find(old_position.first.value);
            CHECK(prod_itr != _producer_tbl.end(), "producer not found")
            _producer_tbl.modify(prod_itr, same_payer, [&](auto& p) {
               CHECK(p.allocating_rewards >= reward, "producer allocating rewards insufficient")
               p.allocating_rewards -= reward;
               p.allocated_rewards += reward;
            });
            v.unclaimed_rewards += reward;
         }
      }

      v.votes = pending_itr->votes;
      v.producers.clear();
      for (const auto& producer : pending_itr->producers) {
         auto checkpoint_itr = checkpoint.find(producer.value);
         CHECK(checkpoint_itr != checkpoint.end(), "missing reward epoch checkpoint")
         v.producers[producer].last_rewards_per_vote = checkpoint_itr->rewards_per_vote;
      }
      v.update_at = current_time_point();
   });
   _pending_vote_tbl.erase(pending_itr);
}

void voter_reward::update_pending_totals(const std::vector<name>& old_producers, int64_t old_votes,
                                         const std::vector<name>& new_producers, int64_t new_votes) {
   const int64_t old_weight = old_producers.empty()
                                ? 0 : old_votes / static_cast<int64_t>(old_producers.size());
   const int64_t new_weight = new_producers.empty()
                                ? 0 : new_votes / static_cast<int64_t>(new_producers.size());

   auto old_itr = old_producers.begin();
   auto new_itr = new_producers.begin();
   while (old_itr != old_producers.end() || new_itr != new_producers.end()) {
      name producer;
      int64_t delta = 0;
      if (new_itr == new_producers.end() ||
          (old_itr != old_producers.end() && *old_itr < *new_itr)) {
         producer = *old_itr++;
         delta = -old_weight;
      } else if (old_itr == old_producers.end() || *new_itr < *old_itr) {
         producer = *new_itr++;
         delta = new_weight;
      } else {
         producer = *old_itr++;
         ++new_itr;
         delta = new_weight - old_weight;
      }

      auto prod_itr = _producer_tbl.find(producer.value);
      CHECK(prod_itr != _producer_tbl.end(), "producer not found")
      auto delta_itr = _producer_delta_tbl.find(producer.value);
      if (delta_itr == _producer_delta_tbl.end()) {
         _producer_delta_tbl.emplace(get_self(), [&](auto& row) {
            row.producer = producer;
            row.votes_delta = delta;
         });
      } else {
         _producer_delta_tbl.modify(delta_itr, same_payer, [&](auto& row) {
            row.votes_delta += delta;
         });
      }
   }
}

void voter_reward::queue_vote_change(const name& voter, int64_t votes,
                                     const std::vector<name>& producers,
                                     uint64_t activation_epoch) {
   auto state = _epoch_state_tbl.exists() ? _epoch_state_tbl.get() : epoch_state{};
   CHECK(votes >= 0, "voter's votes can not be negative")

   auto voter_itr = _voter_tbl.find(voter.value);
   CHECK(voter_itr != _voter_tbl.end(), "voter info not found")

   if (activation_epoch == 0) {
      const int64_t old_weight = voter_itr->producers.empty()
                                   ? 0 : voter_itr->votes / static_cast<int64_t>(voter_itr->producers.size());
      const int64_t new_weight = producers.empty()
                                   ? 0 : votes / static_cast<int64_t>(producers.size());
      _voter_tbl.modify(voter_itr, voter, [&](auto& v) {
         allocate_producer_rewards(v.producers, old_weight, -old_weight, voter,
                                   v.unclaimed_rewards);
         v.producers.clear();
         for (const auto& producer : producers) {
            voted_producer_map added;
            added[producer] = {};
            allocate_producer_rewards(added, 0, new_weight, voter, v.unclaimed_rewards);
            v.producers.emplace(*added.begin());
         }
         v.votes = votes;
         v.update_at = current_time_point();
      });
      return;
   }

   CHECK(activation_epoch == state.next_epoch_to_close + 1,
         "vote change must activate in the next reward epoch")

   auto pending_itr = _pending_vote_tbl.find(voter.value);

   std::vector<name> old_producers;
   int64_t old_votes = 0;
   if (pending_itr != _pending_vote_tbl.end()) {
      old_producers = pending_itr->producers;
      old_votes = pending_itr->votes;
   } else {
      old_votes = voter_itr->votes;
      for (const auto& item : voter_itr->producers) old_producers.push_back(item.first);
   }
   update_pending_totals(old_producers, old_votes, producers, votes);

   if (pending_itr == _pending_vote_tbl.end()) {
      _pending_vote_tbl.emplace(voter, [&](auto& row) {
         row.owner = voter;
         row.votes = votes;
         row.producers = producers;
         row.activation_epoch = activation_epoch;
      });
   } else {
      _pending_vote_tbl.modify(pending_itr, voter, [&](auto& row) {
         row.votes = votes;
         row.producers = producers;
         row.activation_epoch = activation_epoch;
      });
   }
}

void voter_reward::change_vote(const name& voter, int64_t votes, bool is_adding,
                               uint64_t activation_epoch) {
   require_auth( SYSTEM_CONTRACT );
   require_auth( voter );

   CHECK(votes > 0, "votes must be positive")

   materialize_pending(voter);
   auto voter_itr = _voter_tbl.find(voter.value);
   if (voter_itr == _voter_tbl.end()) {
      _voter_tbl.emplace(voter, [&](auto& v) {
         v.owner = voter;
         v.unclaimed_rewards = asset(0, core_symbol());
         v.claimed_rewards = asset(0, core_symbol());
      });
      voter_itr = _voter_tbl.find(voter.value);
   }

   auto pending_itr = _pending_vote_tbl.find(voter.value);
   const int64_t base_votes = pending_itr == _pending_vote_tbl.end()
                                ? voter_itr->votes : pending_itr->votes;
   std::vector<name> target_producers;
   if (pending_itr != _pending_vote_tbl.end()) {
      target_producers = pending_itr->producers;
   } else {
      for (const auto& item : voter_itr->producers) target_producers.push_back(item.first);
   }

   const int64_t target_votes = is_adding ? base_votes + votes : base_votes - votes;
   CHECK(target_votes >= 0, "voter's votes insufficent")
   queue_vote_change(voter, target_votes, target_producers, activation_epoch);
}

void voter_reward::allocate_producer_rewards(voted_producer_map& producers, int64_t weight_old,
                                                                                    int64_t weight_delta, const name& new_payer, asset &allocated_rewards_out)
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
         if (rewards_per_vote_delta > 0 && weight_old > 0) {
            ASSERT(weight_old <= p.votes)
            asset new_rewards = calc_voter_rewards(weight_old, rewards_per_vote_delta);
            CHECK(p.allocating_rewards >= new_rewards, "producer allocating rewards insufficient");
            p.allocating_rewards -= new_rewards;
            p.allocated_rewards += new_rewards;

            ASSERT(p.total_rewards == p.allocating_rewards + p.allocated_rewards)

            allocated_rewards_out += new_rewards; // update allocated_rewards for voter
         }

         p.votes += weight_delta;
         CHECK(p.votes >= 0, "producer votes can not be negative")
         p.update_at = now;

         last_rewards_per_vote = p.rewards_per_vote; // update for voted_prod
      });
   }
}

void voter_reward::check_init() const {
   CHECK( _gstate.total_rewards.symbol.is_valid(), "reward contract has not been initialized" )
}

const symbol& voter_reward::core_symbol() const {
   return _gstate.total_rewards.symbol;
}

} /// namespace flon

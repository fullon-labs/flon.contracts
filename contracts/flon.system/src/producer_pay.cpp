#include <flon.system/flon.system.hpp>
#include <flon.token/flon.token.hpp>
#include <voter.reward/voter.reward.hpp>

namespace eosiosystem {

   using eosio::current_time_point;
   using eosio::microseconds;
   using eosio::token;


   inline constexpr int64_t power(int64_t base, int64_t exp) {
      int64_t ret = 1;
      while( exp > 0  ) {
         ret *= base; --exp;
      }
      return ret;
   }

   uint64_t system_contract::current_reward_epoch( const time_point& now ) const {
      check(_gstate.reward_started_time != time_point(), "producer rewards have not started");
      check(now >= _gstate.reward_started_time, "producer rewards have not started");
      const block_timestamp now_slot(now);
      const block_timestamp start_slot(_gstate.reward_started_time);
      return (now_slot.slot - start_slot.slot) / reward_epoch_slots;
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

      #ifdef ENABLE_VOTING_PRODUCER
      /** check producer reward started */
      if( _gstate.election_activated_time == time_point() || timestamp < _gstate.election_activated_time )
         return;



      time_point now = timestamp;

      // Close at most one overdue epoch per block. Under normal operation this
      // runs exactly once at the first block of a new six-hour epoch; the
      // bounded catch-up behavior avoids an unbounded onblock loop after a gap.
      if (_gstate.reward_started_time != time_point() && now >= _gstate.reward_started_time) {
         reward_epoch_singleton epoch_state_tbl(get_self(), get_self().value);
         const auto epoch_state = epoch_state_tbl.exists()
                                    ? epoch_state_tbl.get() : reward_epoch_state{};
         if (epoch_state.next_epoch_to_close < current_reward_epoch(now)) {
            closeepoch(epoch_state.next_epoch_to_close);
         }
      }

      /**
       * At startup the initial producer may not be one that is registered / elected
       * and therefore there may be no producer object for them.
       */
      auto prod = _producers.find( producer.value );
      if ( prod != _producers.end() && _gstate.reward_started_time != time_point() && now >= _gstate.reward_started_time ) {
         // cur_period start at 0
         int64_t cur_period = (now - _gstate.reward_started_time).to_seconds() / reward_halving_period_seconds;
         auto rewards_per_block = _gstate.initial_rewards_per_block.amount / power(2, cur_period);
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

      /// only update block producers once every minute, block_timestamp is in half seconds
      if( timestamp.slot - _gstate.last_producer_schedule_update.slot > 120 ) {
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
   void system_contract::claimrewards( const name& owner ) {
      require_auth( owner );

      auto prod = _producers.find( owner.value );
      check( prod != _producers.end(), "producer not found" );
      check( prod->unclaimed_rewards.amount > 0, "no rewards to claim" );

      asset producer_rewards = prod->unclaimed_rewards;

      // Update producer/global state before inline transfers to avoid re-entrant duplicate claims.
      _producers.modify( prod, same_payer, [&](auto& p ) {
         p.unclaimed_rewards.amount = 0;
         p.last_claim_time = current_time_point();
      });
      _gstate.total_unclaimed_rewards.amount -= producer_rewards.amount;

      // Transfer producer rewards to owner.
      if (producer_rewards.amount > 0) {
         token::transfer_action transfer_act( token_account, {{get_self(), active_permission}} );
         transfer_act.send( get_self(), owner, producer_rewards, "producer rewards" );
      }
   }

   void system_contract::closeepoch( uint64_t epoch_id ) {
      check(_gstate.reward_started_time != time_point(), "producer rewards have not started");
      check(current_reward_epoch(current_time_point()) > epoch_id, "reward epoch has not ended");

      reward_epoch_singleton epoch_state_tbl(get_self(), get_self().value);
      auto epoch_state = epoch_state_tbl.exists() ? epoch_state_tbl.get() : reward_epoch_state{};
      check(epoch_id == epoch_state.next_epoch_to_close, "reward epochs must be closed sequentially");

      epoch_rewards_table epoch_rewards(get_self(), epoch_id);
      for (auto itr = epoch_rewards.begin(); itr != epoch_rewards.end();) {
         const auto producer_name = itr->producer;
         const auto gross_rewards = itr->rewards;
         auto prod = _producers.find(producer_name.value);
         check(prod != _producers.end(), "epoch reward producer not found");

         asset shared_rewards(0, gross_rewards.symbol);
         if (itr->reward_shared_ratio > 0) {
            const int128_t shared_amount = static_cast<int128_t>(gross_rewards.amount) *
                                           itr->reward_shared_ratio / 10000;
            check(shared_amount <= std::numeric_limits<int64_t>::max(),
                  "shared reward overflow");
            shared_rewards.amount = static_cast<int64_t>(shared_amount);
         }
         const asset producer_rewards = gross_rewards - shared_rewards;

         if (producer_rewards.amount > 0) {
            _producers.modify(prod, same_payer, [&](auto& p) {
               p.unclaimed_rewards += producer_rewards;
            });
         }

         if (shared_rewards.amount > 0) {
            token::transfer_action transfer_act(token_account, {{get_self(), active_permission}});
            transfer_act.send(get_self(), voter_reward_account, shared_rewards,
                              producer_name.to_string());
            _gstate.total_unclaimed_rewards.amount -= shared_rewards.amount;
         }

         itr = epoch_rewards.erase(itr);
      }

      flon::voter_reward::closeepoch_action close_reward_epoch{
         voter_reward_account, {{get_self(), active_permission}}
      };
      close_reward_epoch.send(epoch_id);

      // Ranking changes submitted during epoch N become active only after N
      // has been rewarded with its immutable vote snapshot.
      pending_vote_deltas_table vote_deltas(get_self(), get_self().value);
      for (auto delta_itr = vote_deltas.begin(); delta_itr != vote_deltas.end();) {
         auto prod = _producers.find(delta_itr->producer.value);
         check(prod != _producers.end(), "pending vote producer not found");
         _producers.modify(prod, same_payer, [&](auto& p) {
            p.total_votes += delta_itr->votes_delta;
            check(p.total_votes >= 0, "producer's elected votes can not be negative");
         });
         delta_itr = vote_deltas.erase(delta_itr);
      }

      epoch_state.next_epoch_to_close += 1;
      epoch_state_tbl.set(epoch_state, get_self());
   }

   void system_contract::cfgelection( const time_point& election_activated_time, const time_point& reward_started_time, const asset& initial_rewards_per_block) {
      require_auth(get_self());

      const auto& core_symb = core_symbol();
      check(initial_rewards_per_block.symbol == core_symb, "reward symbol mismatch with core symbol");
      check(initial_rewards_per_block.amount >= 0, "reward can not be negative");

      const auto& now = eosio::current_time_point();
      if (_gstate.election_activated_time == time_point() || _gstate.election_activated_time > now ) {
         check(election_activated_time > now, "election activated time must larger than now");
         _gstate.election_activated_time = election_activated_time;
      } else {
         check( election_activated_time == _gstate.election_activated_time,
            "can not change election activated time after election has already been activated");
      }

      check(reward_started_time >= _gstate.election_activated_time, "reward start time can not less than election activated time");
      if (_gstate.reward_started_time == time_point() || _gstate.reward_started_time > now ) {
         check(reward_started_time > now, "reward start time must larger than now");
         _gstate.reward_started_time = reward_started_time;
         _gstate.initial_rewards_per_block = initial_rewards_per_block;
      } else {
         check( reward_started_time == _gstate.reward_started_time,
            "can not change reward start time after reward has already been started");
         check( initial_rewards_per_block == _gstate.initial_rewards_per_block,
            "can not change initial rewards per block after reward has already been started");
      }
   }
   #endif//ENABLE_VOTING_PRODUCER
} //namespace eosiosystem

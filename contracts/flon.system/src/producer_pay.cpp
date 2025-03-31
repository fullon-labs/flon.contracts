#include <flon.system/flon.system.hpp>
#include <flon.token/flon.token.hpp>

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

      // _gstate2.last_block_num is not used anywhere in the system contract code anymore.
      // Although this field is deprecated, we will continue updating it for now until the last_block_num field
      // is eventually completely removed, at which point this line can be removed.
      _gstate2.last_block_num = timestamp;

      /** check producer reward started */
      if( _gstate.election_activated_time == time_point() || timestamp < _gstate.election_activated_time )
         return;



      time_point now = timestamp;

      /**
       * At startup the initial producer may not be one that is registered / elected
       * and therefore there may be no producer object for them.
       */
      auto prod = _producers.find( producer.value );
      if ( prod != _producers.end() && _gstate.reward_started_time != time_point() && now >= _gstate.reward_started_time ) {
         // cur_period start at 0
         int64_t cur_period = (now - _gstate.reward_started_time).to_seconds() / reward_halving_period_seconds;
         auto rewards_per_block = _gstate.initial_rewards_per_block.amount / power(2, cur_period);
         _producers.modify( prod, same_payer, [&](auto& p ) {
            p.unclaimed_rewards.amount += rewards_per_block;
         });
         _gstate.total_produced_rewards.amount += rewards_per_block;
         _gstate.total_unclaimed_rewards.amount += rewards_per_block;
      }

      /// only update block producers once every minute, block_timestamp is in half seconds
      if( timestamp.slot - _gstate.last_producer_schedule_update.slot > 120 ) {
         update_elected_producers( timestamp );

         if( (timestamp.slot - _gstate.last_name_close.slot) > blocks_per_day ) {
            name_bid_table bids(get_self(), get_self().value);
            auto idx = bids.get_index<"highbid"_n>();
            auto highest = idx.lower_bound( std::numeric_limits<uint64_t>::max()/2 );
            if( highest != idx.end() &&
                highest->high_bid > 0 &&
                (current_time_point() - highest->last_bid_time) > microseconds(useconds_per_day)
            ) {
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
      }
   }

   void system_contract::claimrewards( const name& owner ) {
      require_auth( owner );
      check(false, "unsupport claimrewards");
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
      } else {
         check( reward_started_time == _gstate.reward_started_time,
            "can not change reward start timee after reward has already been started");
      }

      _gstate.initial_rewards_per_block = initial_rewards_per_block;
   }
} //namespace eosiosystem
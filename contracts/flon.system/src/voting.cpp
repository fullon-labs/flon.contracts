#include <eosio/crypto.hpp>
#include <eosio/datastream.hpp>
#include <eosio/eosio.hpp>
#include <eosio/multi_index.hpp>
#include <eosio/permission.hpp>
#include <eosio/privileged.hpp>
#include <eosio/serialize.hpp>
#include <eosio/singleton.hpp>

#include <flon.system/flon.system.hpp>
#include <flon.token/flon.token.hpp>
#include <flon.reward/flon.reward.hpp>

#include <type_traits>
#include <limits>
#include <set>
#include <algorithm>
#include <cmath>

namespace eosiosystem {

   using eosio::const_mem_fun;
   using eosio::current_time_point;
   using eosio::indexed_by;
   using eosio::microseconds;
   using eosio::seconds;
   using eosio::singleton;
   using std::to_string;

   void system_contract::register_producer(  const name& producer,
                                             const block_signing_authority& producer_authority,
                                             const string& url,
                                             uint16_t location,
                                             optional<uint32_t> reward_shared_ratio ) {

      check( url.size() < 512, "url too long" );
      if (reward_shared_ratio) {
         CHECK(*reward_shared_ratio <= ratio_boost, "reward_shared_ratio is too large than " + to_string(ratio_boost));
      }

      const auto& core_sym = core_symbol();
      auto prod = _producers.find( producer.value );
      const auto ct = current_time_point();

      eosio::public_key producer_key{};

      std::visit( [&](auto&& auth ) {
         if( auth.keys.size() == 1 ) {
            // if the producer_authority consists of a single key, use that key in the legacy producer_key field
            producer_key = auth.keys[0].key;
         }
      }, producer_authority );

      if (!flon::flon_reward::is_producer_registered(reward_account, producer)) {
         flon::flon_reward::regproducer_action reg_act{ reward_account, { {producer, active_permission} } };
         reg_act.send( producer );
      }

      if ( prod != _producers.end() ) {
         _producers.modify( prod, producer, [&]( producer_info& info ){
            info.producer_key       = producer_key;
            info.is_active          = true;
            info.url                = url;
            info.location           = location;
            info.producer_authority = producer_authority;
            if ( info.last_claim_time == time_point() )
               info.last_claim_time = ct;
            if (reward_shared_ratio)
               info.reward_shared_ratio = *reward_shared_ratio;
         });

      } else {
         _producers.emplace( producer, [&]( producer_info& info ){
            info.owner              = producer;
            info.total_votes        = 0;
            info.producer_key       = producer_key;
            info.is_active          = true;
            info.url                = url;
            info.location           = location;
            info.last_claim_time    = ct;
            info.unclaimed_rewards  = asset(0, core_sym);
            info.producer_authority = producer_authority;
            if (reward_shared_ratio)
               info.reward_shared_ratio = *reward_shared_ratio;
         });
      }

   }

   void system_contract::regproducer(  const name& producer, const eosio::public_key& producer_key,
                                       const string& url, uint16_t location,
                                       optional<uint32_t> reward_shared_ratio ) {
      require_auth( producer );
      check( url.size() < 512, "url too long" );

      register_producer( producer, convert_to_block_signing_authority( producer_key ), url, location, reward_shared_ratio );
   }

   void system_contract::regproducer2( const name& producer,
                                       const block_signing_authority& producer_authority,
                                       const string& url,
                                       uint16_t location,
                                       optional<uint32_t> reward_shared_ratio) {
      require_auth( producer );
      check( url.size() < 512, "url too long" );

      std::visit( [&](auto&& auth ) {
         check( auth.is_valid(), "invalid producer authority" );
      }, producer_authority );

      register_producer( producer, producer_authority, url, location, reward_shared_ratio );
   }

   void system_contract::unregprod( const name& producer ) {
      require_auth( producer );

      const auto& prod = _producers.get( producer.value, "producer not found" );
      _producers.modify( prod, same_payer, [&]( producer_info& info ){
         info.deactivate();
      });
   }

   void system_contract::update_elected_producers( const block_timestamp& block_time ) {
      _gstate.last_producer_schedule_update = block_time;

      auto idx = _producers.get_index<"prototalvote"_n>();

      using value_type = std::pair<eosio::producer_authority, uint16_t>;
      std::vector< value_type > top_producers;
      std::vector< finalizer_auth_info > proposed_finalizers;
      top_producers.reserve(21);
      proposed_finalizers.reserve(21);

      bool is_savanna = is_savanna_consensus();

      for( auto it = idx.cbegin(); it != idx.cend() && top_producers.size() < 21 && 0 < it->total_votes && it->active(); ++it ) {
         if( is_savanna ) {
            auto finalizer = _finalizers.find( it->owner.value );
            if( finalizer == _finalizers.end() ) {
               // The producer is not in finalizers table, indicating it does not have an
               // active registered finalizer key. Try next one.
               continue;
            }

            // This should never happen. Double check just in case
            if( finalizer->active_key_binary.empty() ) {
               continue;
            }

            proposed_finalizers.emplace_back(*finalizer);
         }

         top_producers.emplace_back(
            eosio::producer_authority{
               .producer_name = it->owner,
               .authority     = it->producer_authority
            },
            it->location
         );
      }

      if( top_producers.size() == 0 || top_producers.size() < _gstate.last_producer_schedule_size ) {
         return;
      }

      std::sort( top_producers.begin(), top_producers.end(), []( const value_type& lhs, const value_type& rhs ) {
         return lhs.first.producer_name < rhs.first.producer_name; // sort by producer name
         // return lhs.second < rhs.second; // sort by location
      } );

      std::vector<eosio::producer_authority> producers;

      producers.reserve(top_producers.size());
      for( auto& item : top_producers )
         producers.push_back( std::move(item.first) );

      if( set_proposed_producers( producers ) >= 0 ) {
         _gstate.last_producer_schedule_size = static_cast<decltype(_gstate.last_producer_schedule_size)>( producers.size() );
      }

      // set_proposed_finalizers() checks if last proposed finalizer policy
      // has not changed, it will not call set_finalizers() host function.
      if( is_savanna ) {
         set_proposed_finalizers( std::move(proposed_finalizers) );
      }
   }

   // double stake2vote( int64_t staked ) {
   //    /// TODO subtract 2080 brings the large numbers closer to this decade
   //    double weight = int64_t( (current_time_point().sec_since_epoch() - (block_timestamp::block_timestamp_epoch / 1000)) / (seconds_per_day * 7) )  / double( 52 );
   //    return double(staked) * std::pow( 2, weight );
   // }

   void system_contract::voteproducer( const name& voter_name, const std::vector<name>& producers ) {

      require_auth(voter_name);

      check( producers.size() <= max_vote_producer_count, "attempt to vote for too many producers" );
      for( size_t i = 1; i < producers.size(); ++i ) {
         check( producers[i - 1] < producers[i], "producer votes must be unique and sorted" );
      }

      auto voter_itr = _voters.find( voter_name.value );
      check( voter_itr != _voters.end(), "voter not found" ); /// addvote creates voter object

      ASSERT( voter_itr->votes >= 0 )
      CHECKC( voter_itr->producers != producers, err::VOTE_CHANGE_ERROR, "producers no change" )
      // if( voter_itr->producers == producers ) return;

      auto now = current_time_point();
      // CHECK( time_point(voter_itr->last_unvoted_time) + seconds(vote_interval_sec) < now, "Voter can only vote or subvote once a day" )

      const auto& old_prods = voter_itr->producers;
      auto old_prod_itr = old_prods.begin();
      auto new_prod_itr = producers.begin();
      std::vector<name> removed_prods; removed_prods.reserve(old_prods.size());
      std::vector<name> modified_prods; removed_prods.reserve(old_prods.size());
      std::vector<name> added_prods;   added_prods.reserve(producers.size());
      while(old_prod_itr != old_prods.end() || new_prod_itr != producers.end()) {

         if (old_prod_itr != old_prods.end() && new_prod_itr != producers.end()) {
            if (old_prod_itr < new_prod_itr) {
               removed_prods.push_back(*old_prod_itr);
               old_prod_itr++;
            } else if (new_prod_itr < old_prod_itr) {
               added_prods.push_back(*new_prod_itr);
               new_prod_itr++;
            } else { // new_prod_itr == old_prod_itr
               modified_prods.push_back(*old_prod_itr);
               old_prod_itr++;
               new_prod_itr++;
            }
         } else if ( old_prod_itr != old_prods.end() ) { //  && new_prod_itr == producers.end()
               removed_prods.push_back(*old_prod_itr);
               old_prod_itr++;
         } else { // new_prod_itr != producers.end() && old_prod_itr == old_prods.end()
               added_prods.push_back(*new_prod_itr);
               new_prod_itr++;
         }
      }

      update_producer_votes(removed_prods, -voter_itr->votes, false);
      update_producer_votes(modified_prods, 0, false);
      update_producer_votes(added_prods, voter_itr->votes, false);

      flon::flon_reward::voteproducer_action voteproducer_act{ reward_account, { {get_self(), active_permission}, {voter_name, active_permission} } };
      voteproducer_act.send( voter_name, producers );

      _voters.modify( voter_itr, same_payer, [&]( auto& v ) {
         v.producers          = producers;
         v.last_unvoted_time  = now;
      });
   }

   void system_contract::update_producer_votes(  const std::vector<name>& producers,
                                                         int64_t votes_delta,
                                                         bool is_adding) {
      for( const auto& p : producers ) {
         auto pitr = _producers.find( p.value );

         CHECK( pitr != _producers.end(), "producer " + p.to_string() + " is not registered" );

         if (votes_delta > 0) {
            CHECK( pitr->active() , "producer " + pitr->owner.to_string() + " is not active" );
         }
         // CHECK(pitr->ext, "producer " + pitr->owner.to_string() + " is not updated by regproducer")

         _producers.modify( pitr, same_payer, [&]( auto& p ) {
            p.total_votes += votes_delta;
            CHECK( p.total_votes >= 0, "producer's elected votes can not be negative" )
            // _elect_gstate.total_producer_elected_votes += votes_delta;
            // CHECK(_elect_gstate.total_producer_elected_votes >= 0, "total_producer_elected_votes can not be negative");
         });
      }
   }

   void system_contract::addvote( const name& voter, const asset& vote_staked ) {
      require_auth(voter);

      CHECK(vote_staked.symbol == core_symbol(), "vote_staked must be core symbol")
      CHECK(vote_staked.amount > 0, "vote_staked must be positive")

      if (_gstate.total_vote_stake.amount == 0) {
         _gstate.total_vote_stake = vote_staked;
      } else {
         _gstate.total_vote_stake += vote_staked;
      }

      auto votes = vote_staked.amount;
      eosio::token::transfer_action transfer_act{ token_account, { {voter, active_permission} } };
      transfer_act.send( voter, vote_account, vote_staked, "addvote" );

      auto now = current_time_point();
      auto voter_itr = _voters.find( voter.value );
      if( voter_itr != _voters.end() ) {
         if (voter_itr->producers.size() > 0) {
            update_producer_votes(voter_itr->producers, votes, false);
         }

         _voters.modify( voter_itr, same_payer, [&]( auto& v ) {
            v.votes             += votes;
         });
      } else {
         _voters.emplace( voter, [&]( auto& v ) {
            v.owner              = voter;
            v.votes              = votes;
         });
      }

      flon::flon_reward::addvote_action addvote_act{ reward_account, { {get_self(), active_permission}, {voter, active_permission} } };
      addvote_act.send( voter, votes );
   }

   void system_contract::subvote( const name& voter, const asset& vote_staked ) {
      require_auth(voter);
      auto now = current_time_point();
      CHECK( _gstate.election_activated_time != time_point() && _gstate.election_activated_time <= now,
             "cannot subvote until the election is activated" );

      CHECK(vote_staked.symbol == core_symbol(), "vote_staked must be core symbol")
      CHECK(vote_staked.amount > 0, "vote_staked must be positive")

      auto votes = vote_staked.amount;
      auto voter_itr = _voters.find( voter.value );
      CHECK( voter_itr != _voters.end(), "voter not found" )

      CHECK( voter_itr->votes >= votes, "votes insufficent" )

      CHECK(_gstate.total_vote_stake >= vote_staked, "Total vote stake insufficent")
      _gstate.total_vote_stake -= vote_staked;

      // CHECKC( time_point(voter_itr->last_unvoted_time) + seconds(vote_interval_sec) < now, err::VOTE_ERROR, "Voter can only vote or subvote once a day" )

      vote_refund_table vote_refund_tbl( get_self(), voter.value );
      CHECKC( vote_refund_tbl.find( voter.value ) == vote_refund_tbl.end(), err::VOTE_REFUND_ERROR, "This account already has a vote refund" );

      update_producer_votes(voter_itr->producers, -votes, false);

      _voters.modify( voter_itr, same_payer, [&]( auto& v ) {
         v.votes             -= votes;
         v.last_unvoted_time  = now;
      });

      flon::flon_reward::subvote_action subvote_act{ reward_account, { {get_self(), active_permission}, {voter, active_permission} } };
      subvote_act.send( voter, votes );

      vote_refund_tbl.emplace( voter, [&]( auto& r ) {
         r.owner = voter;
         r.vote_staked = vote_staked;
         r.request_time = now;
      });

      static const name act_name = "voterefund"_n;
      uint128_t trx_send_id = uint128_t(act_name.value) << 64 | voter.value;
      eosio::transaction refund_trx;
      auto pl = permission_level{ voter, active_permission };
      refund_trx.actions.emplace_back( pl, _self, act_name, voter );
      refund_trx.delay_sec = refund_delay_sec;
      refund_trx.send( trx_send_id, voter, true );
   }

   void system_contract::voterefund( const name& owner ) {
      vote_refund_table vote_refund_tbl( get_self(), owner.value );
      auto itr = vote_refund_tbl.find( owner.value );
      check( itr != vote_refund_tbl.end(), "no vote refund found" );
      check( itr->request_time + seconds(refund_delay_sec) <= current_time_point(), "refund period not mature yet" );

      eosio::token::transfer_action transfer_act{ token_account, { {vote_account, active_permission} } };
      transfer_act.send( vote_account, itr->owner, itr->vote_staked, "voterefund" );
      vote_refund_tbl.erase( itr );
   }

} /// namespace eosiosystem

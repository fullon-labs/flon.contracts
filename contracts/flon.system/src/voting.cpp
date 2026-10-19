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
#include <voter.reward/voter.reward.hpp>

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

   static uint64_t next_vote_epoch(const eosio_global_state& state) {
      const auto now = current_time_point();
      if (state.reward_started_time == time_point() || now < state.reward_started_time) return 0;
      const block_timestamp now_slot(now);
      const block_timestamp start_slot(state.reward_started_time);
      return (now_slot.slot - start_slot.slot) / reward_epoch_slots + 1;
   }

   #ifdef ENABLE_VOTING_PRODUCER
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

      if (!flon::voter_reward::is_producer_registered(voter_reward_account, producer)) {
         flon::voter_reward::regproducer_action reg_act{ voter_reward_account, { {producer, active_permission} } };
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

      materialize_pending_vote(voter_name);
      auto voter_itr = _voters.find( voter_name.value );
      check( voter_itr != _voters.end(), "voter not found" ); /// addvote creates voter object

      ASSERT( voter_itr->votes >= 0 )
      pending_voters_table pending_votes(get_self(), get_self().value);
      auto pending_itr = pending_votes.find(voter_name.value);
      const int64_t target_votes = pending_itr == pending_votes.end()
                                     ? voter_itr->votes : pending_itr->votes;
      const auto& current_target = pending_itr == pending_votes.end()
                                     ? voter_itr->producers : pending_itr->producers;
      CHECKC( current_target != producers, err::VOTE_CHANGE_ERROR, "producers no change" )

      auto now = current_time_point();
      // CHECK( time_point(voter_itr->last_unvoted_time) + seconds(vote_interval_sec) < now, "Voter can only vote or subvote once a day" )

      const uint64_t activation_epoch = next_vote_epoch(_gstate);
      queue_vote_change(voter_name, target_votes, producers, activation_epoch);

      flon::voter_reward::voteproducer_action voteproducer_act{ voter_reward_account, { {get_self(), active_permission}, {voter_name, active_permission} } };
      voteproducer_act.send( voter_name, producers, activation_epoch );

      _voters.modify( voter_itr, same_payer, [&]( auto& v ) { v.last_unvoted_time = now; });
   }

   void system_contract::update_vote_allocations( const std::vector<name>& old_producers, int64_t old_votes,
                                                   const std::vector<name>& new_producers, int64_t new_votes ) {
      ASSERT( old_votes >= 0 && new_votes >= 0 )

      // One locked-token unit creates at most one aggregate unit of ranking power.
      // Integer division intentionally leaves the remainder unallocated.
      const int64_t old_weight = old_producers.empty() ? 0 : old_votes / static_cast<int64_t>(old_producers.size());
      const int64_t new_weight = new_producers.empty() ? 0 : new_votes / static_cast<int64_t>(new_producers.size());

      auto old_itr = old_producers.begin();
      auto new_itr = new_producers.begin();
      while( old_itr != old_producers.end() || new_itr != new_producers.end() ) {
         if( new_itr == new_producers.end() ||
             (old_itr != old_producers.end() && *old_itr < *new_itr) ) {
            update_producer_votes( {*old_itr}, -old_weight );
            ++old_itr;
         } else if( old_itr == old_producers.end() || *new_itr < *old_itr ) {
            update_producer_votes( {*new_itr}, new_weight );
            ++new_itr;
         } else {
            update_producer_votes( {*old_itr}, new_weight - old_weight );
            ++old_itr;
            ++new_itr;
         }
      }
   }

   void system_contract::update_producer_votes( const std::vector<name>& producers,
                                                int64_t votes_delta ) {
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

   void system_contract::materialize_pending_vote( const name& voter ) {
      pending_voters_table pending_votes(get_self(), get_self().value);
      auto pending_itr = pending_votes.find(voter.value);
      if (pending_itr == pending_votes.end()) return;

      reward_epoch_singleton epoch_state_tbl(get_self(), get_self().value);
      auto epoch_state = epoch_state_tbl.exists() ? epoch_state_tbl.get() : reward_epoch_state{};
      if (pending_itr->activation_epoch > epoch_state.next_epoch_to_close) return;

      auto voter_itr = _voters.find(voter.value);
      CHECK(voter_itr != _voters.end(), "voter not found")
      _voters.modify(voter_itr, same_payer, [&](auto& row) {
         row.votes = pending_itr->votes;
         row.producers = pending_itr->producers;
      });
      pending_votes.erase(pending_itr);
   }

   void system_contract::update_pending_vote_deltas(const std::vector<name>& old_producers,
                                                     int64_t old_votes,
                                                     const std::vector<name>& new_producers,
                                                     int64_t new_votes) {
      const int64_t old_weight = old_producers.empty()
                                   ? 0 : old_votes / static_cast<int64_t>(old_producers.size());
      const int64_t new_weight = new_producers.empty()
                                   ? 0 : new_votes / static_cast<int64_t>(new_producers.size());
      pending_vote_deltas_table deltas(get_self(), get_self().value);

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

         auto delta_itr = deltas.find(producer.value);
         if (delta_itr == deltas.end()) {
            deltas.emplace(get_self(), [&](auto& row) {
               row.producer = producer;
               row.votes_delta = delta;
            });
         } else {
            deltas.modify(delta_itr, same_payer, [&](auto& row) { row.votes_delta += delta; });
         }
      }
   }

   void system_contract::queue_vote_change(const name& voter, int64_t votes,
                                            const std::vector<name>& producers,
                                            uint64_t activation_epoch) {
      CHECK(votes >= 0, "voter's votes can not be negative")
      pending_voters_table pending_votes(get_self(), get_self().value);
      auto pending_itr = pending_votes.find(voter.value);
      auto voter_itr = _voters.find(voter.value);
      CHECK(voter_itr != _voters.end(), "voter not found")

      if (activation_epoch == 0) {
         update_vote_allocations(voter_itr->producers, voter_itr->votes, producers, votes);
         _voters.modify(voter_itr, same_payer, [&](auto& row) {
            row.votes = votes;
            row.producers = producers;
         });
         return;
      }

      std::vector<name> old_producers;
      int64_t old_votes = 0;
      if (pending_itr == pending_votes.end()) {
         old_producers = voter_itr->producers;
         old_votes = voter_itr->votes;
      } else {
         old_producers = pending_itr->producers;
         old_votes = pending_itr->votes;
      }
      update_pending_vote_deltas(old_producers, old_votes, producers, votes);

      if (pending_itr == pending_votes.end()) {
         pending_votes.emplace(voter, [&](auto& row) {
            row.voter = voter;
            row.votes = votes;
            row.producers = producers;
            row.activation_epoch = activation_epoch;
         });
      } else {
         pending_votes.modify(pending_itr, voter, [&](auto& row) {
            row.votes = votes;
            row.producers = producers;
            row.activation_epoch = activation_epoch;
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

      asset glon_quantity(votes, symbol(symbol_code("GLON"), vote_staked.symbol.precision()));
      eosio::token::issue_action issue_glon_act{ token_account, { {get_self(), active_permission} } };
      issue_glon_act.send( get_self(), glon_quantity, "governance receipt for vote stake" );

      eosio::token::transfer_action transfer_glon_act{ token_account, { {get_self(), active_permission} } };
      transfer_glon_act.send( get_self(), voter, glon_quantity, "governance receipt for vote stake" );

      materialize_pending_vote(voter);
      auto voter_itr = _voters.find( voter.value );
      if( voter_itr != _voters.end() ) {
      } else {
         _voters.emplace( voter, [&]( auto& v ) {
            v.owner              = voter;
            v.votes              = 0;
         });
         voter_itr = _voters.find(voter.value);
      }

      pending_voters_table pending_votes(get_self(), get_self().value);
      auto pending_itr = pending_votes.find(voter.value);
      const int64_t target_votes = (pending_itr == pending_votes.end()
                                      ? voter_itr->votes : pending_itr->votes) + votes;
      const auto target_producers = pending_itr == pending_votes.end()
                                      ? voter_itr->producers : pending_itr->producers;
      const uint64_t activation_epoch = next_vote_epoch(_gstate);
      queue_vote_change(voter, target_votes, target_producers, activation_epoch);

      flon::voter_reward::addvote_action addvote_act{ voter_reward_account, { {get_self(), active_permission}, {voter, active_permission} } };
      addvote_act.send( voter, votes, activation_epoch );
   }

   void system_contract::subvote( const name& voter, const asset& vote_staked ) {
      require_auth(voter);
      auto now = current_time_point();
      CHECK( _gstate.election_activated_time != time_point() && _gstate.election_activated_time <= now,
             "cannot subvote until the election is activated" );

      CHECK(vote_staked.symbol == core_symbol(), "vote_staked must be core symbol")
      CHECK(vote_staked.amount > 0, "vote_staked must be positive")

      auto votes = vote_staked.amount;
      materialize_pending_vote(voter);
      auto voter_itr = _voters.find( voter.value );
      CHECK( voter_itr != _voters.end(), "voter not found" )

      pending_voters_table pending_votes(get_self(), get_self().value);
      auto pending_itr = pending_votes.find(voter.value);
      const int64_t current_target_votes = pending_itr == pending_votes.end()
                                             ? voter_itr->votes : pending_itr->votes;
      const auto target_producers = pending_itr == pending_votes.end()
                                      ? voter_itr->producers : pending_itr->producers;
      CHECK( current_target_votes >= votes, "votes insufficent" )

      CHECK(_gstate.total_vote_stake >= vote_staked, "Total vote stake insufficent")
      _gstate.total_vote_stake -= vote_staked;

      // CHECKC( time_point(voter_itr->last_unvoted_time) + seconds(vote_interval_sec) < now, err::VOTE_ERROR, "Voter can only vote or subvote once a day" )

      vote_refund_table vote_refund_tbl( get_self(), voter.value );
      CHECKC( vote_refund_tbl.find( voter.value ) == vote_refund_tbl.end(), err::VOTE_REFUND_ERROR, "This account already has a vote refund" );

      asset glon_quantity(votes, symbol(symbol_code("GLON"), vote_staked.symbol.precision()));
      eosio::token::transfer_action return_glon_act{
         token_account, { {voter, active_permission}, {get_self(), active_permission} }
      };
      return_glon_act.send( voter, get_self(), glon_quantity, "return governance receipt" );

      eosio::token::retire_action retire_glon_act{ token_account, { {get_self(), active_permission} } };
      retire_glon_act.send( glon_quantity, "retire governance receipt" );

      const uint64_t activation_epoch = next_vote_epoch(_gstate);
      queue_vote_change(voter, current_target_votes - votes, target_producers, activation_epoch);

      _voters.modify( voter_itr, same_payer, [&]( auto& v ) {
         v.last_unvoted_time  = now;
      });

      flon::voter_reward::subvote_action subvote_act{ voter_reward_account, { {get_self(), active_permission}, {voter, active_permission} } };
      subvote_act.send( voter, votes, activation_epoch );

      vote_refund_tbl.emplace( voter, [&]( auto& r ) {
         r.owner = voter;
         r.vote_staked = vote_staked;
         r.request_time = now;
      });

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

   #else

   void system_contract::setprods( const std::vector<eosio::producer_authority>& schedule ) {
      require_auth( get_self() );
      set_proposed_producers( schedule );
   }

   #endif//ENABLE_VOTING_PRODUCER
} /// namespace eosiosystem

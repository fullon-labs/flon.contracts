#include <eosio/datastream.hpp>
#include <eosio/eosio.hpp>
#include <eosio/multi_index.hpp>
#include <eosio/privileged.hpp>
#include <eosio/serialize.hpp>
#include <eosio/transaction.hpp>

#include <flon.system/flon.system.hpp>
#include <flon.token/flon.token.hpp>

namespace eosiosystem {

   using eosio::asset;
   using eosio::const_mem_fun;
   using eosio::current_time_point;
   using eosio::indexed_by;
   using eosio::permission_level;
   using eosio::seconds;
   using eosio::time_point_sec;
   using eosio::token;

   /**
    * Buy self gas action, gas can only be purchased to itself.
    */
   void system_contract::buygasself( const name& account, const asset& quant ) {
      return buygas( account, account, quant );
   }

   /**
    *  When buying gas the payer irreversibly transfers quant to system contract. The receiver pays for the
    *  storage of all database records associated with this action.
    *
    */
   void system_contract::buygas( const name& payer, const name& receiver, const asset& quant )
   {
      require_auth( payer );

      check( quant.symbol == core_symbol(), "must buy gas with core token" );
      check( quant.amount > 0, "must purchase a positive amount" );
      check( is_account(receiver), "receiver account not exists" );
      // TODO: limit the max quant?

      //TODO: fee of buygas?
      // asset fee = quant;
      // fee.amount = ( fee.amount + 199 ) / 200; /// .5% fee (round up)
      // fee.amount cannot be 0 since that is only possible if quant.amount is 0 which is not allowed by the assert above.
      // If quant.amount == 1, then fee.amount == 1,
      // otherwise if quant.amount > 1, then 0 < fee.amount < quant.amount.
      // asset quant_after_fee = quant;
      // quant_after_fee.amount -= fee.amount;
      // quant_after_fee.amount should be > 0 if quant.amount > 1.
      {
         // TODO: should open gas_account of flon.token in init()
         token::transfer_action transfer_act{ token_account, { {payer, active_permission}, {gas_account, active_permission} } };
         transfer_act.send( payer, gas_account, quant, "buy gas" );
      }
      // if ( fee.amount > 0 ) {
      //    token::transfer_action transfer_act{ token_account, { {payer, active_permission} } };
      //    transfer_act.send( payer, gasfee_account, fee, "gas fee" );
      //    channel_to_system_fees( gasfee_account, fee );
      // }
      uint64_t reserved_gas = 0;
      bool is_unlimited = true;
      eosio::get_resource_limits( receiver, reserved_gas, is_unlimited );

      check( std::numeric_limits<uint64_t>::max() - reserved_gas >= (uint64_t)quant.amount,
                  "Overflow when calculating new gas");
      // TODO: limit max reserved_gas ?
      reserved_gas +=  (uint64_t)quant.amount;

      eosio::set_resource_limits( receiver, reserved_gas, is_unlimited );
   }

   void system_contract::changebw( name from, const name& receiver,
                                   const asset& stake_net_delta, const asset& stake_cpu_delta, bool transfer )
   {
      require_auth( from );
      check( stake_net_delta.amount != 0 || stake_cpu_delta.amount != 0, "should stake non-zero amount" );
      check( std::abs( (stake_net_delta + stake_cpu_delta).amount )
             >= std::max( std::abs( stake_net_delta.amount ), std::abs( stake_cpu_delta.amount ) ),
             "net and cpu deltas cannot be opposite signs" );

      name source_stake_from = from;
      if ( transfer ) {
         from = receiver;
      }

      update_stake_delegated( from, receiver, stake_net_delta, stake_cpu_delta );
      update_user_resources( from, receiver, stake_net_delta, stake_cpu_delta );

      // create refund or update from existing refund
      if ( stake_account != source_stake_from ) { //for eosio both transfer and refund make no sense
         refunds_table refunds_tbl( get_self(), from.value );
         auto req = refunds_tbl.find( from.value );

         //create/update/delete refund
         auto net_balance = stake_net_delta;
         auto cpu_balance = stake_cpu_delta;

         // net and cpu are same sign by assertions in delegatebw and undelegatebw
         // redundant assertion also at start of changebw to protect against misuse of changebw
         bool is_undelegating = (net_balance.amount + cpu_balance.amount ) < 0;
         bool is_delegating_to_self = (!transfer && from == receiver);

         if( is_delegating_to_self || is_undelegating ) {
            if ( req != refunds_tbl.end() ) { //need to update refund
               refunds_tbl.modify( req, same_payer, [&]( refund_request& r ) {
                  if ( net_balance.amount < 0 || cpu_balance.amount < 0 ) {
                     r.request_time = current_time_point();
                  }
                  r.net_amount -= net_balance;
                  if ( r.net_amount.amount < 0 ) {
                     net_balance = -r.net_amount;
                     r.net_amount.amount = 0;
                  } else {
                     net_balance.amount = 0;
                  }
                  r.cpu_amount -= cpu_balance;
                  if ( r.cpu_amount.amount < 0 ){
                     cpu_balance = -r.cpu_amount;
                     r.cpu_amount.amount = 0;
                  } else {
                     cpu_balance.amount = 0;
                  }
               });

               check( 0 <= req->net_amount.amount, "negative net refund amount" ); //should never happen
               check( 0 <= req->cpu_amount.amount, "negative cpu refund amount" ); //should never happen

               if ( req->is_empty() ) {
                  refunds_tbl.erase( req );
               }
            } else if ( net_balance.amount < 0 || cpu_balance.amount < 0 ) { //need to create refund
               refunds_tbl.emplace( from, [&]( refund_request& r ) {
                  r.owner = from;
                  if ( net_balance.amount < 0 ) {
                     r.net_amount = -net_balance;
                     net_balance.amount = 0;
                  } else {
                     r.net_amount = asset( 0, core_symbol() );
                  }
                  if ( cpu_balance.amount < 0 ) {
                     r.cpu_amount = -cpu_balance;
                     cpu_balance.amount = 0;
                  } else {
                     r.cpu_amount = asset( 0, core_symbol() );
                  }
                  r.request_time = current_time_point();
               });
            } // else stake increase requested with no existing row in refunds_tbl -> nothing to do with refunds_tbl
         } /// end if is_delegating_to_self || is_undelegating

         auto transfer_amount = net_balance + cpu_balance;
         if ( 0 < transfer_amount.amount ) {
            token::transfer_action transfer_act{ token_account, { {source_stake_from, active_permission} } };
            transfer_act.send( source_stake_from, stake_account, asset(transfer_amount), "stake bandwidth" );
         }
      }

      // const int64_t staked = update_voting_power( from, stake_net_delta + stake_cpu_delta );

      if( _voters.find( from.value ) == _voters.end() ) {
         _voters.emplace( from, [&]( auto& v ) {
            v.owner  = from;
         });
      }
   }

   void system_contract::update_stake_delegated( const name from, const name receiver, const asset stake_net_delta, const asset stake_cpu_delta )
   {
      del_bandwidth_table del_tbl( get_self(), from.value );
      auto itr = del_tbl.find( receiver.value );
      if( itr == del_tbl.end() ) {
         itr = del_tbl.emplace( from, [&]( auto& dbo ){
               dbo.from          = from;
               dbo.to            = receiver;
               dbo.net_weight    = stake_net_delta;
               dbo.cpu_weight    = stake_cpu_delta;
            });
      } else {
         del_tbl.modify( itr, same_payer, [&]( auto& dbo ){
               dbo.net_weight    += stake_net_delta;
               dbo.cpu_weight    += stake_cpu_delta;
            });
      }
      check( 0 <= itr->net_weight.amount, "insufficient staked net bandwidth" );
      check( 0 <= itr->cpu_weight.amount, "insufficient staked cpu bandwidth" );
      if ( itr->is_empty() ) {
         del_tbl.erase( itr );
      }
   }

   void system_contract::update_user_resources( const name from, const name receiver, const asset stake_net_delta, const asset stake_cpu_delta )
   {
      user_resources_table   totals_tbl( get_self(), receiver.value );
      auto tot_itr = totals_tbl.find( receiver.value );
      if( tot_itr ==  totals_tbl.end() ) {
         tot_itr = totals_tbl.emplace( from, [&]( auto& tot ) {
               tot.owner = receiver;
               tot.net_weight    = stake_net_delta;
               tot.cpu_weight    = stake_cpu_delta;
            });
      } else {
         totals_tbl.modify( tot_itr, from == receiver ? from : same_payer, [&]( auto& tot ) {
               tot.net_weight    += stake_net_delta;
               tot.cpu_weight    += stake_cpu_delta;
            });
      }
      check( 0 <= tot_itr->net_weight.amount, "insufficient staked total net bandwidth" );
      check( 0 <= tot_itr->cpu_weight.amount, "insufficient staked total cpu bandwidth" );

      {
         bool ram_managed = false;
         bool net_managed = false;
         bool cpu_managed = false;

         auto voter_itr = _voters.find( receiver.value );
         if( voter_itr != _voters.end() ) {
            ram_managed = has_field( voter_itr->flags1, voter_info::flags1_fields::ram_managed );
            net_managed = has_field( voter_itr->flags1, voter_info::flags1_fields::net_managed );
            cpu_managed = has_field( voter_itr->flags1, voter_info::flags1_fields::cpu_managed );
         }

         if( !(net_managed && cpu_managed) ) {
            int64_t ram_bytes, net, cpu;
            // get_resource_limits( receiver, ram_bytes, net, cpu );

            // set_resource_limits( receiver,
            //                      ram_managed ? ram_bytes : std::max( tot_itr->ram_bytes + ram_gift_bytes, ram_bytes ),
            //                      net_managed ? net : tot_itr->net_weight.amount,
            //                      cpu_managed ? cpu : tot_itr->cpu_weight.amount );
         }
      }

      if ( tot_itr->is_empty() ) {
         totals_tbl.erase( tot_itr );
      } // tot_itr can be invalid, should go out of scope
   }

   void system_contract::delegatebw( const name& from, const name& receiver,
                                     const asset& stake_net_quantity,
                                     const asset& stake_cpu_quantity, bool transfer )
   {
      asset zero_asset( 0, core_symbol() );
      check( stake_cpu_quantity >= zero_asset, "must stake a positive amount" );
      check( stake_net_quantity >= zero_asset, "must stake a positive amount" );
      check( stake_net_quantity.amount + stake_cpu_quantity.amount > 0, "must stake a positive amount" );
      check( !transfer || from != receiver, "cannot use transfer flag if delegating to self" );

      changebw( from, receiver, stake_net_quantity, stake_cpu_quantity, transfer);
   } // delegatebw

   void system_contract::undelegatebw( const name& from, const name& receiver,
                                       const asset& unstake_net_quantity, const asset& unstake_cpu_quantity )
   {
      asset zero_asset( 0, core_symbol() );
      check( unstake_cpu_quantity >= zero_asset, "must unstake a positive amount" );
      check( unstake_net_quantity >= zero_asset, "must unstake a positive amount" );
      check( unstake_cpu_quantity.amount + unstake_net_quantity.amount > 0, "must unstake a positive amount" );
      // check( _gstate.election_activated_time != time_point(),
      //        "cannot undelegate bandwidth until the chain is activated (at least 15% of all tokens participate in voting)" );

      changebw( from, receiver, -unstake_net_quantity, -unstake_cpu_quantity, false);
   } // undelegatebw

   void system_contract::refund( const name& owner ) {
      require_auth( owner );

      refunds_table refunds_tbl( get_self(), owner.value );
      auto req = refunds_tbl.find( owner.value );
      check( req != refunds_tbl.end(), "refund request not found" );
      check( req->request_time + seconds(refund_delay_sec) <= current_time_point(),
             "refund is not available yet" );
      token::transfer_action transfer_act{ token_account, { {stake_account, active_permission}, {req->owner, active_permission} } };
      transfer_act.send( stake_account, req->owner, req->net_amount + req->cpu_amount, "unstake" );
      refunds_tbl.erase( req );
   }

} //namespace eosiosystem

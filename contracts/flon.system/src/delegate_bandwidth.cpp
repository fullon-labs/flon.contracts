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

      // core_symbol() will check system contract is_init()
      check( quant.symbol == core_symbol(), "must buy gas with core asset" );
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

} //namespace eosiosystem

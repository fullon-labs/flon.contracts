#include <flon.system/flon.system.hpp>
#include <flon.token/flon.token.hpp>
#include <flon.reward/flon.reward.hpp>

#include <eosio/crypto.hpp>
#include <eosio/dispatcher.hpp>

#include <cmath>

namespace eosiosystem {

   using eosio::current_time_point;
   using eosio::token;

   double get_continuous_rate(int64_t annual_rate) {
      return std::log1p(double(annual_rate)/double(100*inflation_precision));
   }

   system_contract::system_contract( name s, name code, datastream<const char*> ds )
   :native(s,code,ds),
    _voters(get_self(), get_self().value),
    _producers(get_self(), get_self().value),
    _producers2(get_self(), get_self().value),
    _finalizer_keys(get_self(), get_self().value),
    _finalizers(get_self(), get_self().value),
    _last_prop_finalizers(get_self(), get_self().value),
    _fin_key_id_generator(get_self(), get_self().value),
    _global(get_self(), get_self().value),
    _global2(get_self(), get_self().value),
    _global3(get_self(), get_self().value),
    _global4(get_self(), get_self().value),
    _schedules(get_self(), get_self().value)
   {
      _gstate  = _global.exists() ? _global.get() : get_default_parameters();
      _gstate2 = _global2.exists() ? _global2.get() : eosio_global_state2{};
      _gstate3 = _global3.exists() ? _global3.get() : eosio_global_state3{};
      _gstate4 = _global4.exists() ? _global4.get() : get_default_inflation_parameters();
   }

   eosio_global_state system_contract::get_default_parameters() {
      eosio_global_state dp;
      get_blockchain_parameters(dp);
      return dp;
   }

   eosio_global_state4 system_contract::get_default_inflation_parameters() {
      eosio_global_state4 gs4;
      gs4.continuous_rate      = get_continuous_rate(default_annual_rate);
      gs4.inflation_pay_factor = default_inflation_pay_factor;
      gs4.votepay_factor       = default_votepay_factor;
      return gs4;
   }

   // symbol system_contract::core_symbol()const {
   //    const static auto sym = get_core_symbol();
   //    return sym;
   // }

   system_contract::~system_contract() {
      _global.set( _gstate, get_self() );
      _global2.set( _gstate2, get_self() );
      _global3.set( _gstate3, get_self() );
      _global4.set( _gstate4, get_self() );
   }

   void system_contract::channel_to_system_fees( const name& from, const asset& amount ) {
      token::transfer_action transfer_act{ token_account, { from, active_permission } };
      transfer_act.send( from, fees_account, amount, "transfer from " + from.to_string() + " to " + fees_account.to_string() );
   }

#ifdef SYSTEM_BLOCKCHAIN_PARAMETERS
   extern "C" [[eosio::wasm_import]] void set_parameters_packed(const void*, size_t);
#endif

   void system_contract::setparams( const blockchain_parameters_t& params ) {
      require_auth( get_self() );
      (eosio::blockchain_parameters&)(_gstate) = params;
      check( 3 <= _gstate.max_authority_depth, "max_authority_depth should be at least 3" );
#ifndef SYSTEM_BLOCKCHAIN_PARAMETERS
      set_blockchain_parameters( params );
#else
      constexpr size_t param_count = 18;
      // an upper bound on the serialized size
      char buf[1 + sizeof(params) + param_count];
      datastream<char*> stream(buf, sizeof(buf));

      stream << uint8_t(17);
      stream << uint8_t(0) << params.max_block_net_usage
             << uint8_t(1) << params.target_block_net_usage_pct
             << uint8_t(2) << params.max_transaction_net_usage
             << uint8_t(3) << params.base_per_transaction_net_usage
             << uint8_t(4) << params.net_usage_leeway
             << uint8_t(5) << params.context_free_discount_net_usage_num
             << uint8_t(6) << params.context_free_discount_net_usage_den

             << uint8_t(7) << params.max_block_cpu_usage
             << uint8_t(8) << params.target_block_cpu_usage_pct
             << uint8_t(9) << params.max_transaction_cpu_usage
             << uint8_t(10) << params.min_transaction_cpu_usage

             << uint8_t(11) << params.max_transaction_lifetime
             << uint8_t(12) << params.deferred_trx_expiration_window
             << uint8_t(13) << params.max_transaction_delay
             << uint8_t(14) << params.max_inline_action_size
             << uint8_t(15) << params.max_inline_action_depth
             << uint8_t(16) << params.max_authority_depth;
      if(params.max_action_return_value_size)
      {
         stream << uint8_t(17) << params.max_action_return_value_size.value();
         ++buf[0];
      }

      set_parameters_packed(buf, stream.tellp());
#endif
   }

#ifdef SYSTEM_CONFIGURABLE_WASM_LIMITS

   // The limits on contract WebAssembly modules
   struct wasm_parameters
   {
      uint32_t max_mutable_global_bytes;
      uint32_t max_table_elements;
      uint32_t max_section_elements;
      uint32_t max_linear_memory_init;
      uint32_t max_func_local_bytes;
      uint32_t max_nested_structures;
      uint32_t max_symbol_bytes;
      uint32_t max_module_bytes;
      uint32_t max_code_bytes;
      uint32_t max_pages;
      uint32_t max_call_depth;
   };

   static constexpr wasm_parameters default_limits = {
       .max_mutable_global_bytes = 1024,
       .max_table_elements = 1024,
       .max_section_elements = 8192,
       .max_linear_memory_init = 64*1024,
       .max_func_local_bytes = 8192,
       .max_nested_structures = 1024,
       .max_symbol_bytes = 8192,
       .max_module_bytes = 20*1024*1024,
       .max_code_bytes = 20*1024*1024,
       .max_pages = 528,
       .max_call_depth = 251
   };

   static constexpr wasm_parameters high_limits = {
       .max_mutable_global_bytes = 8192,
       .max_table_elements = 8192,
       .max_section_elements = 8192,
       .max_linear_memory_init = 16*64*1024,
       .max_func_local_bytes = 8192,
       .max_nested_structures = 1024,
       .max_symbol_bytes = 8192,
       .max_module_bytes = 20*1024*1024,
       .max_code_bytes = 20*1024*1024,
       .max_pages = 528,
       .max_call_depth = 1024
   };

   extern "C" [[eosio::wasm_import]] void set_wasm_parameters_packed( const void*, size_t );

   void set_wasm_parameters( const wasm_parameters& params )
   {
      char buf[sizeof(uint32_t) + sizeof(params)] = {};
      memcpy(buf + sizeof(uint32_t), &params, sizeof(params));
      set_wasm_parameters_packed( buf, sizeof(buf) );
   }

   void system_contract::wasmcfg( const name& settings )
   {
      require_auth( get_self() );
      if( settings == "default"_n || settings == "low"_n )
      {
         set_wasm_parameters( default_limits );
      }
      else if( settings == "high"_n )
      {
         set_wasm_parameters( high_limits );
      }
      else
      {
         check(false, "Unknown configuration");
      }
   }

#endif

   void system_contract::setpriv( const name& account, uint8_t ispriv ) {
      require_auth( get_self() );
      set_privileged( account, ispriv );
   }

   void system_contract::setalimits( const name& account, uint64_t gas, bool is_unlimited ) {
      require_auth( get_self() );
      check( is_account(account), "account not exists" );

      // TODO: validate gas range?
      uint64_t gas_old = 0;
      bool is_unlimited_old = true;
      eosio::get_resource_limits( account, gas_old, is_unlimited_old );

      check( gas != gas_old && is_unlimited != is_unlimited_old, "data does not have change");
      eosio::set_resource_limits( account, gas, is_unlimited );
   }

   void system_contract::activate( const eosio::checksum256& feature_digest ) {
      require_auth( get_self() );
      preactivate_feature( feature_digest );
   }

   void system_contract::logsystemfee( const name& protocol, const asset& fee, const std::string& memo ) {
      require_auth( get_self() );
   }

   void system_contract::rmvproducer( const name& producer ) {
      require_auth( get_self() );
      auto prod = _producers.find( producer.value );
      check( prod != _producers.end(), "producer not found" );
      _producers.modify( prod, same_payer, [&](auto& p) {
            p.deactivate();
         });
   }

   void system_contract::updtrevision( uint8_t revision ) {
      require_auth( get_self() );
      check( _gstate2.revision < 255, "can not increment revision" ); // prevent wrap around
      check( revision == _gstate2.revision + 1, "can only increment revision by one" );
      check( revision <= 1, // set upper bound to greatest revision supported in the code
             "specified revision is not yet supported by the code" );
      _gstate2.revision = revision;
   }

   void system_contract::setinflation( int64_t annual_rate, int64_t inflation_pay_factor, int64_t votepay_factor ) {
      require_auth(get_self());
      check(annual_rate >= 0, "annual_rate can't be negative");
      if ( inflation_pay_factor < pay_factor_precision ) {
         check( false, "inflation_pay_factor must not be less than " + std::to_string(pay_factor_precision) );
      }
      if ( votepay_factor < pay_factor_precision ) {
         check( false, "votepay_factor must not be less than " + std::to_string(pay_factor_precision) );
      }
      _gstate4.continuous_rate      = get_continuous_rate(annual_rate);
      _gstate4.inflation_pay_factor = inflation_pay_factor;
      _gstate4.votepay_factor       = votepay_factor;
      _global4.set( _gstate4, get_self() );
   }

   void system_contract::setpayfactor( int64_t inflation_pay_factor, int64_t votepay_factor ) {
      require_auth(get_self());
      if ( inflation_pay_factor < pay_factor_precision ) {
         check( false, "inflation_pay_factor must not be less than " + std::to_string(pay_factor_precision) );
      }
      if ( votepay_factor < pay_factor_precision ) {
         check( false, "votepay_factor must not be less than " + std::to_string(pay_factor_precision) );
      }
      _gstate4.inflation_pay_factor = inflation_pay_factor;
      _gstate4.votepay_factor       = votepay_factor;
      _global4.set( _gstate4, get_self() );
   }

   void system_contract::setschedule( const time_point_sec start_time, double continuous_rate )
   {
      require_auth( get_self() );

      check(continuous_rate >= 0, "continuous_rate can't be negative");
      check(continuous_rate <= 1, "continuous_rate can't be over 100%");

      auto itr = _schedules.find( start_time.sec_since_epoch() );

      if( itr == _schedules.end() ) {
         _schedules.emplace( get_self(), [&]( auto& s ) {
            s.start_time = start_time;
            s.continuous_rate = continuous_rate;
         });
      } else {
         _schedules.modify( itr, same_payer, [&]( auto& s ) {
            s.continuous_rate = continuous_rate;
         });
      }
   }

   void system_contract::delschedule( const time_point_sec start_time )
   {
      require_auth( get_self() );

      auto itr = _schedules.require_find( start_time.sec_since_epoch(), "schedule not found" );
      _schedules.erase( itr );
   }

   void system_contract::execschedule()
   {
      check(execute_next_schedule(), "no schedule to execute");
   }

   bool system_contract::execute_next_schedule()
   {
      auto itr = _schedules.begin();
      if (itr == _schedules.end()) return false; // no schedules to execute

      if ( current_time_point().sec_since_epoch() >= itr->start_time.sec_since_epoch() ) {
         _gstate4.continuous_rate = itr->continuous_rate;
         _global4.set( _gstate4, get_self() );
         _schedules.erase( itr );
         return true;
      }
      return false;
   }

   /**
    *  Called after a new account is created. This code enforces resource-limits rules
    *  for new accounts as well as new account naming conventions.
    *
    *  Account names containing '.' symbols must have a suffix equal to the name of the creator.
    *  This allows users who buy a premium name (shorter than 12 characters with no dots) to be the only ones
    *  who can create accounts with the creator's name as a suffix.
    *
    */
   void native::newaccount( const name&       creator,
                            const name&       new_account_name,
                            ignore<authority> owner,
                            ignore<authority> active ) {

      if( creator != get_self() ) {
         uint64_t tmp = new_account_name.value >> 4;
         bool has_dot = false;

         for( uint32_t i = 0; i < 12; ++i ) {
           has_dot |= !(tmp & 0x1f);
           tmp >>= 5;
         }
         if( has_dot ) { // or is less than 12 characters
            auto suffix = new_account_name.suffix();
            if( suffix == new_account_name ) {
               name_bid_table bids(get_self(), get_self().value);
               auto current = bids.find( new_account_name.value );
               check( current != bids.end(), "no active bid for name" );
               check( current->high_bidder == creator, "only highest bidder can claim" );
               check( current->high_bid < 0, "auction for name is not closed yet" );
               bids.erase( current );
            } else {
               check( creator == suffix, "only suffix may create this account" );
            }
         }
      }

      // TODO: new user table
      // user_resources_table  userres( get_self(), new_account_name.value );

      // make sure the new account is resource limited.
      set_resource_limits( new_account_name, 0, false );
   }

   void native::setabi( const name& acnt, const std::vector<char>& abi,
                        const binary_extension<std::string>& memo ) {
      eosio::multi_index< "abihash"_n, abi_hash >  table(get_self(), get_self().value);
      auto itr = table.find( acnt.value );
      if( itr == table.end() ) {
         table.emplace( acnt, [&]( auto& row ) {
            row.owner = acnt;
            row.hash = eosio::sha256(const_cast<char*>(abi.data()), abi.size());
         });
      } else {
         table.modify( itr, same_payer, [&]( auto& row ) {
            row.hash = eosio::sha256(const_cast<char*>(abi.data()), abi.size());
         });
      }
   }

   void system_contract::init( unsigned_int version, const symbol& core ) {
      require_auth( get_self() );
      check( version.value == 0, "unsupported version for init action" );

      check( !_gstate.total_vote_stake.symbol.is_valid(), "system contract has already been initialized" );

      _gstate.total_vote_stake.symbol = core;
      _gstate.initial_rewards_per_block.symbol = core;
      _gstate.total_produced_rewards.symbol = core;
      _gstate.total_unclaimed_rewards.symbol = core;

      auto system_token_supply   = eosio::token::get_supply(token_account, core.code() );
      check( system_token_supply.symbol == core, "specified core symbol does not exist (precision mismatch)" );
      check( system_token_supply.amount > 0, "system token supply must be greater than 0" );

      // init gas account' core asset in token contract here,
      // so that gas fees can be deducted automatically for resource usage in native of chain
      check( is_account(token_account), "gas account does not exist");
      token::open_action open_act{ token_account, { {get_self(), active_permission} } };
      open_act.send( gas_account, core, get_self() );

      flon::flon_reward::init_action init_act{ reward_account, { {get_self(), active_permission} } };
      init_act.send( core );
   }

} /// flon.system

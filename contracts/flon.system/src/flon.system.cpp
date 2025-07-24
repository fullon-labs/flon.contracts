#include <flon.system/flon.system.hpp>
#include <flon.token/flon.token.hpp>
#include <flon.reward/flon.reward.hpp>

#include <eosio/crypto.hpp>
#include <eosio/dispatcher.hpp>

#ifdef ENABLE_CONTRACT_VERSION
#include <contract_version.hpp>
#endif//ENABLE_CONTRACT_VERSION

#include <cmath>

namespace eosiosystem {

   using eosio::current_time_point;
   using eosio::token;


   #ifdef ENABLE_CONTRACT_VERSION
   DEFINE_VERSION_CONTRACT_CLASS("flon.system", system_contract)
   #endif//ENABLE_CONTRACT_VERSION

   system_contract::system_contract( name s, name code, datastream<const char*> ds )
   :native(s,code,ds),
   // _users(get_self(), get_self().value),
   #ifdef ENABLE_VOTING_PRODUCER
    _voters(get_self(), get_self().value),
    _producers(get_self(), get_self().value),
    _finalizer_keys(get_self(), get_self().value),
    _finalizers(get_self(), get_self().value),
    _last_prop_finalizers(get_self(), get_self().value),
    _fin_key_id_generator(get_self(), get_self().value),
   #endif//ENABLE_VOTING_PRODUCER
    _global(get_self(), get_self().value)
   {
      _gstate  = _global.exists() ? _global.get() : get_default_parameters();
   }

   eosio_global_state system_contract::get_default_parameters() {
      eosio_global_state dp;
      get_blockchain_parameters(dp);
      return dp;
   }

   // symbol system_contract::core_symbol()const {
   //    const static auto sym = get_core_symbol();
   //    return sym;
   // }

   system_contract::~system_contract() {
      _global.set( _gstate, get_self() );
   }

   void system_contract::channel_to_system_fees( const name& from, const asset& amount ) {
      token::transfer_action transfer_act{ token_account, { from, active_permission } };
      transfer_act.send( from, fees_account, amount, "transfer from " + from.to_string() + " to " + fees_account.to_string() );
   }

#ifdef SYSTEM_BLOCKCHAIN_PARAMETERS

   //order must match parameters as ids are used in serialization
   enum chain_config_params_v0 {
      max_block_net_usage_id,
      target_block_net_usage_pct_id,
      max_transaction_net_usage_id,
      base_per_transaction_net_usage_id,
      net_usage_leeway_id,
      context_free_discount_net_usage_num_id,
      context_free_discount_net_usage_den_id,
      max_block_cpu_usage_id,
      target_block_cpu_usage_pct_id,
      max_transaction_cpu_usage_id,
      min_transaction_cpu_usage_id,
      max_transaction_lifetime_id,
      deferred_trx_expiration_window_id,
      max_transaction_delay_id,
      max_inline_action_size_id,
      max_inline_action_depth_id,
      max_authority_depth_id,
      max_total_ram_usage_id,
      gas_per_cpu_ms_id,
      gas_per_net_kb_id,
      gas_per_ram_kb_id,
      CHAIN_CONFIG_PARAMS_V0_COUNT
   };


   enum chain_config_params_v1 {
      max_action_return_value_size_id = CHAIN_CONFIG_PARAMS_V0_COUNT,
      CHAIN_CONFIG_PARAMS_V1_COUNT
   };

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
      static_assert(CHAIN_CONFIG_PARAMS_V0_COUNT > 1);
      // an upper bound on the serialized size
      char buf[1 + sizeof(params) + CHAIN_CONFIG_PARAMS_V0_COUNT];
      datastream<char*> stream(buf, sizeof(buf));

      stream << uint8_t(CHAIN_CONFIG_PARAMS_V0_COUNT);
      stream << uint8_t(max_block_net_usage_id)                   << params.max_block_net_usage
             << uint8_t(target_block_net_usage_pct_id)            << params.target_block_net_usage_pct
             << uint8_t(max_transaction_net_usage_id)             << params.max_transaction_net_usage
             << uint8_t(base_per_transaction_net_usage_id)        << params.base_per_transaction_net_usage
             << uint8_t(net_usage_leeway_id)                      << params.net_usage_leeway
             << uint8_t(context_free_discount_net_usage_num_id)   << params.context_free_discount_net_usage_num
             << uint8_t(context_free_discount_net_usage_den_id)   << params.context_free_discount_net_usage_den

             << uint8_t(max_block_cpu_usage_id)                   << params.max_block_cpu_usage
             << uint8_t(target_block_cpu_usage_pct_id)            << params.target_block_cpu_usage_pct
             << uint8_t(max_transaction_cpu_usage_id)             << params.max_transaction_cpu_usage
             << uint8_t(min_transaction_cpu_usage_id)             << params.min_transaction_cpu_usage

             << uint8_t(max_transaction_lifetime_id)              << params.max_transaction_lifetime
             << uint8_t(deferred_trx_expiration_window_id)        << params.deferred_trx_expiration_window
             << uint8_t(max_transaction_delay_id)                 << params.max_transaction_delay
             << uint8_t(max_inline_action_size_id)                << params.max_inline_action_size
             << uint8_t(max_inline_action_depth_id)               << params.max_inline_action_depth
             << uint8_t(max_authority_depth_id)                   << params.max_authority_depth
             << uint8_t(max_total_ram_usage_id)                   << params.max_total_ram_usage
             << uint8_t(gas_per_cpu_ms_id)                        << params.gas_per_cpu_ms
             << uint8_t(gas_per_net_kb_id)                        << params.gas_per_net_kb
             << uint8_t(gas_per_ram_kb_id)                        << params.gas_per_ram_kb;
      if(params.max_action_return_value_size)
      {
         stream << uint8_t(max_action_return_value_size_id)       << params.max_action_return_value_size.value();
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

      check( gas != gas_old || is_unlimited != is_unlimited_old, "data does not have change");
      eosio::set_resource_limits( account, gas, is_unlimited );
   }

   void system_contract::activate( const eosio::checksum256& feature_digest ) {
      require_auth( get_self() );
      preactivate_feature( feature_digest );
   }

   void system_contract::logsystemfee( const name& protocol, const asset& fee, const std::string& memo ) {
      require_auth( get_self() );
   }

   #ifdef ENABLE_VOTING_PRODUCER
   void system_contract::rmvproducer( const name& producer ) {
      require_auth( get_self() );
      auto prod = _producers.find( producer.value );
      check( prod != _producers.end(), "producer not found" );
      _producers.modify( prod, same_payer, [&](auto& p) {
            p.deactivate();
         });
   }
   #endif//ENABLE_VOTING_PRODUCER

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
                            ignore<authority> active )
   {
      if (!system_contract::is_init(get_self())) return;

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

               #ifdef ENABLE_NAME_BID
               name_bid_table bids(get_self(), get_self().value);
               auto current = bids.find( new_account_name.value );
               check( current != bids.end(), "no active bid for name" );
               check( current->high_bidder == creator, "only highest bidder can claim" );
               check( current->high_bid < 0, "auction for name is not closed yet" );
               bids.erase( current );
               #else
               check(false, "name bid not supported");
               #endif//ENABLE_NAME_BID
            } else {
               check( creator == suffix, "only suffix may create this account" );
            }
         }
      }

      // TODO: new user table
      users_table  users( get_self(), get_self().value );
      users.emplace( new_account_name, [&]( auto& u ) {
         u.owner     = new_account_name;
         u.creator   = creator;
       });

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

      #ifdef ENABLE_VOTING_PRODUCER
      flon::flon_reward::init_action init_act{ reward_account, { {get_self(), active_permission} } };
      init_act.send( core );
      #endif//ENABLE_VOTING_PRODUCER
   }


   void system_contract::setibintervl( uint64_t idle_block_interval_ms ) {
      require_auth( get_self() );

      producing_config_singleton pcs(get_self(), get_self().value);
      producing_config conf = pcs.get_or_default();
      check( conf.idle_block_interval_ms != idle_block_interval_ms, "idle block interval unchanged" );
      
      conf.idle_block_interval_ms = idle_block_interval_ms;
      pcs.set( conf, get_self() );
   }
} /// flon.system

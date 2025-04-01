#pragma once

#include "contracts.hpp"
#include "test_symbol.hpp"
#include <eosio/chain/abi_serializer.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/testing/tester.hpp>
#include <eosio/chain/config.hpp>

#include <fc/variant_object.hpp>
#include <fstream>

using namespace eosio::chain;
using namespace eosio::testing;
using namespace fc;

using mvo = fc::mutable_variant_object;

namespace eosio_system {


class base_system_tester : public validating_tester {
public:

   base_system_tester(): validating_tester({}, nullptr, setup_policy::none) {

      const auto& pfm = control->get_protocol_feature_manager();
      auto preactivate_feature_digest = pfm.get_builtin_digest(builtin_protocol_feature_t::preactivate_feature);
      schedule_protocol_features_wo_preactivation( { *preactivate_feature_digest } );
      produce_block();

      set_code(config::system_account_name, contracts::boot_wasm());
      set_abi(config::system_account_name, contracts::boot_abi().data());
      produce_block();
      preactivate_all_builtin_protocol_features();

      produce_block();
      set_code(config::system_account_name, contracts::bios_wasm());
      set_abi(config::system_account_name, contracts::bios_abi().data());
      // BLS voting is slow. Use only 1 finalizer for default testser.
      finalizer_keys fin_keys(*this, 1u /* num_keys */, 1u /* finset_size */);
      fin_keys.activate_savanna(0u /* first_key_idx */);
   }
};

class eosio_system_tester : public base_system_tester {
public:

   void basic_setup() {
      produce_block();

      create_accounts({ "flon.token"_n, "flon.ram"_n, "flon.stake"_n,
               "flon.vpay"_n, "flon.saving"_n, "flon.names"_n, "flon.fees"_n,
               "flon.reward"_n, "flon.vote"_n });


      produce_blocks( 100 );

      set_code( "flon.token"_n, contracts::token_wasm());
      set_abi( "flon.token"_n, contracts::token_abi().data() );
      {
         const auto& accnt = control->db().get<account_object,by_name>( "flon.token"_n );
         abi_def abi;
         BOOST_REQUIRE_EQUAL(abi_serializer::to_abi(accnt.abi, abi), true);
         token_abi_ser.set_abi(abi, abi_serializer::create_yield_function(abi_serializer_max_time));
      }

      set_code( "flon.reward"_n, contracts::reward_wasm());
      set_abi( "flon.reward"_n, contracts::reward_abi().data() );
   }

   void create_core_token( symbol core_symbol = symbol{CORE_SYM} ) {
      FC_ASSERT( core_symbol.decimals() == 4, "create_core_token assumes core token has 4 digits of precision" );
      create_currency( "flon.token"_n, config::system_account_name, asset(100000000000000, core_symbol) );
      issue( asset(10000000000000, core_symbol) );
      BOOST_REQUIRE_EQUAL( asset(10000000000000, core_symbol), get_balance( "flon", core_symbol ) );
   }

   void deploy_contract( bool call_init = true ) {
      set_code( config::system_account_name, contracts::system_wasm() );
      set_abi( config::system_account_name, contracts::system_abi().data() );
      if( call_init ) {
         base_tester::push_action(config::system_account_name, "init"_n,
                                               config::system_account_name,  mutable_variant_object()
                                               ("version", 0)
                                               ("core", CORE_SYM_STR)
         );
      }

      {
         const auto& accnt = control->db().get<account_object,by_name>( config::system_account_name );
         abi_def abi;
         BOOST_REQUIRE_EQUAL(abi_serializer::to_abi(accnt.abi, abi), true);
         abi_ser.set_abi(abi, abi_serializer::create_yield_function(abi_serializer_max_time));
      }
   }

   void remaining_setup() {
      produce_block();

      // Assumes previous setup steps were done with core token symbol set to CORE_SYM
      create_account_with_resources( "alice1111111"_n, config::system_account_name, core_sym::from_string("1.0000"), false );
      create_account_with_resources( "bob111111111"_n, config::system_account_name, core_sym::from_string("0.4500"), false );
      create_account_with_resources( "carol1111111"_n, config::system_account_name, core_sym::from_string("1.0000"), false );

      BOOST_REQUIRE_EQUAL( core_sym::from_string("1000000000.0000"), get_balance("flon") + get_balance("flon.stake") + get_balance("flon.ram") + get_balance("flon.fees") );
   }

   enum class setup_level {
      none,
      minimal,
      core_token,
      deploy_contract,
      full
   };

   eosio_system_tester( setup_level l = setup_level::full, setup_policy policy = setup_policy::full )
   : base_system_tester() {
      if( l == setup_level::none ) return;

      basic_setup();
      if( l == setup_level::minimal ) return;

      create_core_token();
      if( l == setup_level::core_token ) return;

      deploy_contract();
      if( l == setup_level::deploy_contract ) return;

      remaining_setup();
   }

   template<typename Lambda>
   eosio_system_tester(Lambda setup) {
      setup(*this);

      basic_setup();
      create_core_token();
      deploy_contract();
      remaining_setup();
   }


   void create_accounts_with_resources( vector<account_name> accounts, account_name creator = config::system_account_name ) {
      for( auto a : accounts ) {
         create_account_with_resources( a, creator );
      }
   }

   transaction_trace_ptr create_account_with_resources( account_name a, account_name creator, uint32_t ram_bytes = 8000 ) {
      signed_transaction trx;
      set_transaction_headers(trx);

      authority owner_auth;
      owner_auth =  authority( get_public_key( a, "owner" ) );

      trx.actions.emplace_back( vector<permission_level>{{creator,config::active_name}},
                                newaccount{
                                   .creator  = creator,
                                   .name     = a,
                                   .owner    = owner_auth,
                                   .active   = authority( get_public_key( a, "active" ) )
                                });

      trx.actions.emplace_back( get_action( config::system_account_name, "buyrambytes"_n, vector<permission_level>{{creator,config::active_name}},
                                            mvo()
                                            ("payer", creator)
                                            ("receiver", a)
                                            ("bytes", ram_bytes) )
                              );
      trx.actions.emplace_back( get_action( config::system_account_name, "delegatebw"_n, vector<permission_level>{{creator,config::active_name}},
                                            mvo()
                                            ("from", creator)
                                            ("receiver", a)
                                            ("stake_net_quantity", core_sym::from_string("10.0000") )
                                            ("stake_cpu_quantity", core_sym::from_string("10.0000") )
                                            ("transfer", 0 )
                                          )
                                );

      set_transaction_headers(trx);
      trx.sign( get_private_key( creator, "active" ), control->get_chain_id()  );
      return push_transaction( trx );
   }

   transaction_trace_ptr create_account_with_resources( account_name a, account_name creator, asset ramfunds, bool multisig,
                                                        asset net = core_sym::from_string("10.0000"), asset cpu = core_sym::from_string("10.0000") ) {
      signed_transaction trx;
      set_transaction_headers(trx);

      authority owner_auth;
      if (multisig) {
         // multisig between account's owner key and creators active permission
         owner_auth = authority(2, {key_weight{get_public_key( a, "owner" ), 1}}, {permission_level_weight{{creator, config::active_name}, 1}});
      } else {
         owner_auth =  authority( get_public_key( a, "owner" ) );
      }

      trx.actions.emplace_back( vector<permission_level>{{creator,config::active_name}},
                                newaccount{
                                   .creator  = creator,
                                   .name     = a,
                                   .owner    = owner_auth,
                                   .active   = authority( get_public_key( a, "active" ) )
                                });

      trx.actions.emplace_back( get_action( config::system_account_name, "buyram"_n, vector<permission_level>{{creator,config::active_name}},
                                            mvo()
                                            ("payer", creator)
                                            ("receiver", a)
                                            ("quant", ramfunds) )
                              );

      trx.actions.emplace_back( get_action( config::system_account_name, "delegatebw"_n, vector<permission_level>{{creator,config::active_name}},
                                            mvo()
                                            ("from", creator)
                                            ("receiver", a)
                                            ("stake_net_quantity", net )
                                            ("stake_cpu_quantity", cpu )
                                            ("transfer", 0 )
                                          )
                                );

      set_transaction_headers(trx);
      trx.sign( get_private_key( creator, "active" ), control->get_chain_id()  );
      return push_transaction( trx );
   }

   /*
    * Doing an idump((trace->action_traces[i].return_value)) will show the full hex
    * Converting return_value to a string will transform it into a series of ordianl values represented by chars
    * fc::to_hex() did not work.
    * Couldn't find a method to convert so wrote my own.
    */
   std::string convert_ordinals_to_hex(const std::string& ordinals) {
      // helper to convert to hex, 2 chars for hex, 3 char null terminator
      char hex_temp[3];
      // return string
      std::string hex_as_chars;

      for (unsigned char c : ordinals) {
         // convert to hex from ordinal
         sprintf(hex_temp, "%x", (int)c);
         if (hex_temp[1] == '\0') {
            hex_temp[1] = hex_temp[0];
            hex_temp[0] = '0';
            // null terminate
            hex_temp[2] = '\0';
         }
         hex_as_chars += hex_temp[0];
         hex_as_chars += hex_temp[1];
      }
      return hex_as_chars;
   }

   std::string convert_json_to_hex(const type_name& type, const std::string& json) {
      // ABI for our return struct
      const char* ramtransfer_return_abi = R"=====(
   {
      "version": "flon::abi/1.2",
      "types": [],
      "structs": [
         {
             "name": "action_return_buyram",
             "base": "",
             "fields": [
                 {
                     "name": "payer",
                     "type": "name"
                 },
                 {
                     "name": "receiver",
                     "type": "name"
                 },
                 {
                     "name": "quantity",
                     "type": "asset"
                 },
                 {
                     "name": "bytes_purchased",
                     "type": "int64"
                 },
                 {
                     "name": "ram_bytes",
                     "type": "int64"
                 },
                 {
                      "name": "fee",
                      "type": "asset"
                  }
             ]
         },
         {
            "name": "action_return_ramtransfer",
            "base": "",
            "fields": [
            {
               "name": "from",
               "type": "name"
            },
            {
               "name": "to",
               "type": "name"
            },
            {
               "name": "bytes",
               "type": "int64"
            },
            {
               "name": "from_ram_bytes",
               "type": "int64"
            },
            {
               "name": "to_ram_bytes",
               "type": "int64"
            }
            ]
         },
         {
             "name": "action_return_sellram",
             "base": "",
             "fields": [
                 {
                     "name": "account",
                     "type": "name"
                 },
                 {
                     "name": "quantity",
                     "type": "asset"
                 },
                 {
                     "name": "bytes_sold",
                     "type": "int64"
                 },
                 {
                     "name": "ram_bytes",
                     "type": "int64"
                 },
                 {
                      "name": "fee",
                      "type": "asset"
                  }
             ]
         },
      ],
      "actions": [],
      "tables": [],
      "ricardian_clauses": [],
      "variants": [],
      "action_results": [
            {
                "name": "buyram",
                "result_type": "action_return_buyram"
            },
            {
                "name": "buyrambytes",
                "result_type": "action_return_buyram"
            },
            {
                "name": "buyramself",
                "result_type": "action_return_buyram"
            },
            {
                "name": "ramburn",
                "result_type": "action_return_ramtransfer"
            },
            {
                "name": "ramtransfer",
                "result_type": "action_return_ramtransfer"
            },
            {
                "name": "sellram",
                "result_type": "action_return_sellram"
            }
      ]
   }
   )=====";

      // create abi to parse return values
      auto abi = fc::json::from_string(ramtransfer_return_abi).as<abi_def>();
      abi_serializer ramtransfer_return_serializer = abi_serializer{std::move(abi), abi_serializer::create_yield_function( abi_serializer_max_time )};

      auto return_json = fc::json::from_string(json);
      auto serialized_bytes = ramtransfer_return_serializer.variant_to_binary(type, return_json, abi_serializer::create_yield_function( abi_serializer_max_time ));
      return fc::to_hex(serialized_bytes);
   }

   action_result buyram( const account_name& payer, account_name receiver, const asset& eosin ) {
      return push_action( payer, "buyram"_n, mvo()( "payer",payer)("receiver",receiver)("quant",eosin) );
   }
   action_result buyram( std::string_view payer, std::string_view receiver, const asset& eosin ) {
      return buyram( account_name(payer), account_name(receiver), eosin );
   }

   void validate_buyram_return(const account_name& payer, account_name receiver, const asset& eosin,
                               const type_name& type, const std::string& json) {
      // create hex return from provided json
      std::string expected_hex = convert_json_to_hex(type, json);
      // initialize string that will hold actual return
      std::string actual_hex;

      // execute transaction and get traces must use base_tester
      auto trace = base_tester::push_action(config::system_account_name, "buyram"_n, payer,
                                            mvo()("payer",payer)("receiver",receiver)("quant",eosin));
      produce_block();

      // confirm we have trances and find the right one (should be trace idx == 0)
      BOOST_REQUIRE_EQUAL(true, chain_has_transaction(trace->id));

      /*
       * This is assignment is giving me grief
       * Doing an idump((trace->action_traces[i].return_value)) will show the full hex
       * Inspecting trace->action_traces[i].return_value shows the hex have been converted to ordinals
       * Couldn't find a method to convert so wrote my own.
       */
      // the first trace always has the return value
      int i = 0;
      std::string copy_trace = std::string(trace->action_traces[i].return_value.begin(), trace->action_traces[i].return_value.end());
      actual_hex = convert_ordinals_to_hex(copy_trace);

      // test fails here actual_hex is
      BOOST_REQUIRE_EQUAL(expected_hex,actual_hex);
   }

   action_result ramtransfer(const account_name& from, const account_name& to, uint32_t bytes, const std::string& memo) {
      return push_action(from, "ramtransfer"_n,
                         mvo()("from", from)("to", to)("bytes", bytes)("memo", memo));
   }

   void validate_ramtransfer_return(const account_name& from, const account_name& to, uint32_t bytes, const std::string& memo,
                                                 const type_name& type, const std::string& json) {
      // create hex return from provided json
      std::string expected_hex = convert_json_to_hex(type, json);
      // initialize string that will hold actual return
      std::string actual_hex;

      // execute transaction and get traces must use base_tester
      auto trace = base_tester::push_action(config::system_account_name, "ramtransfer"_n, from,
                                            mvo()("from", from)("to", to)("bytes", bytes)("memo", memo));
      produce_block();

      // confirm we have trances and find the right one (should be trace idx == 0)
      BOOST_REQUIRE_EQUAL(true, chain_has_transaction(trace->id));

      // the first trace always has the return value
      int i = 0;
      std::string copy_trace = std::string(trace->action_traces[i].return_value.begin(), trace->action_traces[i].return_value.end());
      actual_hex = convert_ordinals_to_hex(copy_trace);

      // test fails here actual_hex is
      BOOST_REQUIRE_EQUAL(expected_hex,actual_hex);
   }

   action_result ramburn(const account_name& owner, uint32_t bytes, const std::string& memo)
   {
      return push_action(owner, "ramburn"_n, mvo()("owner", owner)("bytes", bytes)("memo", memo));
   }
   action_result ramburn(std::string_view owner, uint32_t bytes, const std::string& memo)
   {
      return ramburn(account_name(owner), bytes, memo);
   }

   action_result buyramburn( const name& payer, const asset& quantity, const std::string& memo)
   {
      return push_action(payer, "buyramburn"_n, mvo()("payer", payer)("quantity", quantity)("memo", memo));
   }

   void validate_buyramburn_return(const name& payer, const asset& quantity,
                               const std::string& memo, const type_name& type, const std::string& json) {
      // create hex return from provided json
      std::string expected_hex = convert_json_to_hex(type, json);
      // initialize string that will hold actual return
      std::string actual_hex;

      // execute transaction and get traces must use base_tester
      auto trace = base_tester::push_action(config::system_account_name, "buyramburn"_n, payer,
                                mvo()("payer",payer)("quantity",quantity)("memo", memo));
      produce_block();

      // confirm we have trances and find the right one (should be trace idx == 0)
      BOOST_REQUIRE_EQUAL(true, chain_has_transaction(trace->id));

      // the first trace always has the return value
      int i = 0;
      std::string copy_trace = std::string(trace->action_traces[i].return_value.begin(), trace->action_traces[i].return_value.end());
      actual_hex = convert_ordinals_to_hex(copy_trace);

      // test fails here actual_hex is
      BOOST_REQUIRE_EQUAL(expected_hex,actual_hex);
   }

   void validate_ramburn_return(const account_name& owner, uint32_t bytes, const std::string& memo,
                                    const type_name& type, const std::string& json) {
      // create hex return from provided json
      std::string expected_hex = convert_json_to_hex(type, json);
      // initialize string that will hold actual return
      std::string actual_hex;

      auto trace = base_tester::push_action(config::system_account_name, "ramburn"_n, owner, mvo()("owner", owner)("bytes", bytes)("memo", memo));
      produce_block();

      // confirm we have trances and find the right one (should be trace idx == 0)
      BOOST_REQUIRE_EQUAL(true, chain_has_transaction(trace->id));

      // the first trace always has the return value
      int i = 0;
      std::string copy_trace = std::string(trace->action_traces[i].return_value.begin(), trace->action_traces[i].return_value.end());
      // convert from string of ordinals to char representation of hex
      actual_hex = convert_ordinals_to_hex(copy_trace);

      // test fails here actual_hex is
      BOOST_REQUIRE_EQUAL(expected_hex,actual_hex);
   }

   action_result buyrambytes(const account_name& payer, account_name receiver, uint32_t numbytes)
   {
      return push_action(payer, "buyrambytes"_n, mvo()("payer", payer)("receiver", receiver)("bytes", numbytes));
   }
   action_result buyrambytes(std::string_view payer, std::string_view receiver, uint32_t numbytes)
   {
      return buyrambytes(account_name(payer), account_name(receiver), numbytes);
   }

   void validate_buyrambytes_return(const account_name& payer, account_name receiver, uint32_t numbytes,
                                const type_name& type, const std::string& json) {
      // create hex return from provided json
      std::string expected_hex = convert_json_to_hex(type, json);
      // initialize string that will hold actual return
      std::string actual_hex;

      auto trace = base_tester::push_action(config::system_account_name, "buyrambytes"_n, payer,
                                            mvo()("payer", payer)("receiver", receiver)("bytes", numbytes));
      produce_block();

      // confirm we have trances and find the right one (should be trace idx == 0)
      BOOST_REQUIRE_EQUAL(true, chain_has_transaction(trace->id));

      // the first trace always has the return value
      int i = 0;
      std::string copy_trace = std::string(trace->action_traces[i].return_value.begin(), trace->action_traces[i].return_value.end());
      // convert from string of ordinals to char representation of hex
      actual_hex = convert_ordinals_to_hex(copy_trace);

      // test fails here actual_hex is
      BOOST_REQUIRE_EQUAL(expected_hex,actual_hex);
   }

   action_result sellram(const account_name& account, uint64_t numbytes)
   {
      return push_action(account, "sellram"_n, mvo()("account", account)("bytes", numbytes));
   }
   action_result sellram(std::string_view account, uint64_t numbytes) { return sellram(account_name(account), numbytes); }

   void validate_sellram_return(const account_name& account, uint32_t numbytes,
                                    const type_name& type, const std::string& json) {
      // create hex return from provided json
      std::string expected_hex = convert_json_to_hex(type, json);
      // initialize string that will hold actual return
      std::string actual_hex;

      auto trace = base_tester::push_action(config::system_account_name, "sellram"_n, account, mvo()("account", account)("bytes", numbytes));
      produce_block();

      // confirm we have trances and find the right one (should be trace idx == 0)
      BOOST_REQUIRE_EQUAL(true, chain_has_transaction(trace->id));

      // the first trace always has the return value
      int i = 0;
      std::string copy_trace = std::string(trace->action_traces[i].return_value.begin(), trace->action_traces[i].return_value.end());
      // convert from string of ordinals to char representation of hex
      actual_hex = convert_ordinals_to_hex(copy_trace);

      // test fails here actual_hex is
      BOOST_REQUIRE_EQUAL(expected_hex,actual_hex);
   }

   action_result buyramself(const account_name& account, const asset& quant)
   {
      return push_action(account, "buyramself"_n, mvo()("account", account)("quant", quant));
   }

   void validate_buyramself_return(const account_name& account, const asset& quant,
                                const type_name& type, const std::string& json) {
      // create hex return from provided json
      std::string expected_hex = convert_json_to_hex(type, json);
      // initialize string that will hold actual return
      std::string actual_hex;

      auto trace = base_tester::push_action(config::system_account_name, "buyramself"_n,  account, mvo()("account", account)("quant", quant));
      produce_block();

      // confirm we have trances and find the right one (should be trace idx == 0)
      BOOST_REQUIRE_EQUAL(true, chain_has_transaction(trace->id));

      // the first trace always has the return value
      int i = 0;
      std::string copy_trace = std::string(trace->action_traces[i].return_value.begin(), trace->action_traces[i].return_value.end());
      // convert from string of ordinals to char representation of hex
      actual_hex = convert_ordinals_to_hex(copy_trace);

      // test fails here actual_hex is
      BOOST_REQUIRE_EQUAL(expected_hex,actual_hex);
   }

   transaction_trace_ptr setup_producer_accounts( const std::vector<account_name>& accounts,
                                                  asset ram = core_sym::from_string("1.0000"),
                                                  asset cpu = core_sym::from_string("80.0000"),
                                                  asset net = core_sym::from_string("80.0000")
                                                )
   {
      account_name creator(config::system_account_name);
      signed_transaction trx;
      set_transaction_headers(trx);

      for (const auto& a: accounts) {
         authority owner_auth( get_public_key( a, "owner" ) );
         trx.actions.emplace_back( vector<permission_level>{{creator,config::active_name}},
                                   newaccount{
                                         .creator  = creator,
                                         .name     = a,
                                         .owner    = owner_auth,
                                         .active   = authority( get_public_key( a, "active" ) )
                                         });

         trx.actions.emplace_back( get_action( config::system_account_name, "buyram"_n, vector<permission_level>{ {creator, config::active_name} },
                                               mvo()
                                               ("payer", creator)
                                               ("receiver", a)
                                               ("quant", ram) )
                                   );

         trx.actions.emplace_back( get_action( config::system_account_name, "delegatebw"_n, vector<permission_level>{ {creator, config::active_name} },
                                               mvo()
                                               ("from", creator)
                                               ("receiver", a)
                                               ("stake_net_quantity", net)
                                               ("stake_cpu_quantity", cpu )
                                               ("transfer", 0 )
                                               )
                                   );
      }

      set_transaction_headers(trx);
      trx.sign( get_private_key( creator, "active" ), control->get_chain_id()  );
      return push_transaction( trx );
   }

   action_result push_action( const account_name& signer, const action_name &name, const variant_object &data, bool auth = true ) {
         string action_type_name = abi_ser.get_action_type(name);

         action act;
         act.account = config::system_account_name;
         act.name = name;
         act.data = abi_ser.variant_to_binary( action_type_name, data, abi_serializer::create_yield_function(abi_serializer_max_time) );

         return base_tester::push_action( std::move(act), (auth ? signer : signer == "bob111111111"_n ? "alice1111111"_n : "bob111111111"_n).to_uint64_t() );
   }

   base_tester::action_result push_actions(vector<action> actions) {
      signed_transaction trx;
      set_transaction_headers(trx);
      trx.actions = std::move(actions);
      set_transaction_headers(trx);
      for (const auto& act : trx.actions) {
         for (const auto& auth : act.authorization ) {
            trx.sign( get_private_key( auth.actor, auth.permission.to_string() ), control->get_chain_id()  );
         }
      }
      try {
         push_transaction(trx);
      } catch (const fc::exception& ex) {
         edump((ex.to_detail_string()));
         return error(ex.top_message()); // top_message() is assumed by many tests; otherwise they fail
         //return error(ex.to_detail_string());
      }
      produce_block();
      BOOST_REQUIRE_EQUAL(true, chain_has_transaction(trx.id()));
      return success();

   }

   vector<permission_level> get_active_perms(const account_name& name) {
      return {{name, config::active_name}};
   }

   action get_transfer_action( const name& from, const name& to, const asset& quantity, const string& memo = "" ) {
      return get_action( "flon.token"_n,
                         "transfer"_n,
                         get_active_perms(from),
                         mutable_variant_object()
                              ("from",     from)
                              ("to",       to )
                              ("quantity", quantity )
                              ("memo", memo )
      );
   }

   action_result stake( const account_name& from, const account_name& to, const asset& net, const asset& cpu ) {
      return push_action( name(from), "delegatebw"_n, mvo()
                          ("from",     from)
                          ("receiver", to)
                          ("stake_net_quantity", net)
                          ("stake_cpu_quantity", cpu)
                          ("transfer", 0 )
      );
   }
   action_result stake( std::string_view from, std::string_view to, const asset& net, const asset& cpu ) {
      return stake( account_name(from), account_name(to), net, cpu );
   }

   action_result stake( const account_name& acnt, const asset& net, const asset& cpu ) {
      return stake( acnt, acnt, net, cpu );
   }
   action_result stake( std::string_view acnt, const asset& net, const asset& cpu ) {
      return stake( account_name(acnt), net, cpu );
   }

   action_result stake_with_transfer( const account_name& from, const account_name& to, const asset& net, const asset& cpu ) {
      return push_action( name(from), "delegatebw"_n, mvo()
                          ("from",     from)
                          ("receiver", to)
                          ("stake_net_quantity", net)
                          ("stake_cpu_quantity", cpu)
                          ("transfer", true )
      );
   }
   action_result stake_with_transfer( std::string_view from, std::string_view to, const asset& net, const asset& cpu ) {
      return stake_with_transfer( account_name(from), account_name(to), net, cpu );
   }

   action_result stake_with_transfer( const account_name& acnt, const asset& net, const asset& cpu ) {
      return stake_with_transfer( acnt, acnt, net, cpu );
   }
   action_result stake_with_transfer( std::string_view acnt, const asset& net, const asset& cpu ) {
      return stake_with_transfer( account_name(acnt), net, cpu );
   }

   action_result unstake( const account_name& from, const account_name& to, const asset& net, const asset& cpu ) {
      return push_action( name(from), "undelegatebw"_n, mvo()
                          ("from",     from)
                          ("receiver", to)
                          ("unstake_net_quantity", net)
                          ("unstake_cpu_quantity", cpu)
      );
   }

   action_result unstake( std::string_view from, std::string_view to, const asset& net, const asset& cpu ) {
      return unstake( account_name(from), account_name(to), net, cpu );
   }

   action_result unstake( const account_name& acnt, const asset& net, const asset& cpu ) {
      return unstake( acnt, acnt, net, cpu );
   }
   action_result unstake( std::string_view acnt, const asset& net, const asset& cpu ) {
      return unstake( account_name(acnt), net, cpu );
   }

   int64_t bancor_convert( int64_t S, int64_t R, int64_t T ) { return double(R) * T  / ( double(S) + T ); };

   int64_t get_net_limit( account_name a ) {
      // int64_t ram_bytes = 0, net = 0, cpu = 0;
      // control->get_resource_limits_manager().get_account_limits( a, ram_bytes, net, cpu );
      return net;
   };

   int64_t get_cpu_limit( account_name a ) {
      // int64_t ram_bytes = 0, net = 0, cpu = 0;
      // control->get_resource_limits_manager().get_account_limits( a, ram_bytes, net, cpu );
      // return cpu;
   };

   action_result deposit( const account_name& owner, const asset& amount ) {
      return push_action( name(owner), "deposit"_n, mvo()
                          ("owner",  owner)
                          ("amount", amount)
      );
   }

   action_result withdraw( const account_name& owner, const asset& amount ) {
      return push_action( name(owner), "withdraw"_n, mvo()
                          ("owner",  owner)
                          ("amount", amount)
      );
   }


   action_result consolidate( const account_name& owner ) {
      return push_action( name(owner), "consolidate"_n, mvo()("owner", owner) );
   }

   fc::variant get_dbw_obj( const account_name& from, const account_name& receiver ) const {
      vector<char> data = get_row_by_account( config::system_account_name, from, "delband"_n, receiver );
      return data.empty() ? fc::variant() : abi_ser.binary_to_variant("delegated_bandwidth", data, abi_serializer::create_yield_function(abi_serializer_max_time));
   }

   action_result bidname( const account_name& bidder, const account_name& newname, const asset& bid ) {
      return push_action( name(bidder), "bidname"_n, mvo()
                          ("bidder",  bidder)
                          ("newname", newname)
                          ("bid", bid)
                          );
   }
   action_result bidname( std::string_view bidder, std::string_view newname, const asset& bid ) {
      return bidname( account_name(bidder), account_name(newname), bid );
   }

   static fc::variant_object producer_parameters_example( int n ) {
      return mutable_variant_object()
         ("max_block_net_usage", 10000000 + n )
         ("target_block_net_usage_pct", 10 + n )
         ("max_transaction_net_usage", 1000000 + n )
         ("base_per_transaction_net_usage", 100 + n)
         ("net_usage_leeway", 500 + n )
         ("context_free_discount_net_usage_num", 1 + n )
         ("context_free_discount_net_usage_den", 100 + n )
         ("max_block_cpu_usage", 10000000 + n )
         ("target_block_cpu_usage_pct", 10 + n )
         ("max_transaction_cpu_usage", 1000000 + n )
         ("min_transaction_cpu_usage", 100 + n )
         ("max_transaction_lifetime", 3600 + n)
         ("deferred_trx_expiration_window", 600 + n)
         ("max_transaction_delay", 10*86400+n)
         ("max_inline_action_size", 4096 + n)
         ("max_inline_action_depth", 4 + n)
         ("max_authority_depth", 6 + n)
         ("max_ram_size", (n % 10 + 1) * 1024 * 1024)
         ("ram_reserve_ratio", 100 + n);
   }

   action_result regproducer( const account_name& acnt, int params_fixture = 1 ) {
      action_result r = push_action( acnt, "regproducer"_n, mvo()
                          ("producer",  acnt )
                          ("producer_key", get_public_key( acnt, "active" ) )
                          ("url", "" )
                          ("location", 0 )
                          ("reward_shared_ratio", 0 )

      );
      BOOST_REQUIRE_EQUAL( success(), r);
      return r;
   }

   action_result vote( const account_name& voter, const std::vector<account_name>& producers, const account_name& proxy = name(0) ) {
      return push_action(voter, "voteproducer"_n, mvo()
                         ("voter",     voter)
                         ("proxy",     proxy)
                         ("producers", producers));
   }
   action_result vote( const account_name& voter, const std::vector<account_name>& producers, std::string_view proxy ) {
      return vote( voter, producers, account_name(proxy) );
   }

   action_result addvote( const account_name& voter, const asset& vote_staked ) {
      return push_action(voter, "addvote"_n, mvo()
                         ("voter",     voter)
                         ("vote_staked", vote_staked));
   }

   action_result addvote( const account_name& voter, const asset& vote_staked1, const asset& vote_staked2 ) {
      return addvote(voter, vote_staked1 + vote_staked2);
   }

   action_result addvote( const string& voter, const asset& vote_staked1, const asset& vote_staked2 ) {
      return addvote(account_name(voter), vote_staked1, vote_staked2);
   }

   action_result addvote( const account_name& from, const account_name& voter, const asset& vote_staked1, const asset& vote_staked2 ) {
      auto vote_staked = vote_staked1 + vote_staked2;
      vector<action> actions;
      if (from != voter) {
         actions.emplace_back(get_transfer_action(from, voter, vote_staked ));
      }
      actions.emplace_back(get_addvote_action(voter, vote_staked ));

      return push_actions(actions);
   }

   action_result addvote( const string& from, const string& voter, const asset& vote_staked1, const asset& vote_staked2 ) {
      return addvote( account_name(from), account_name(voter), vote_staked1, vote_staked2 );
   }

   action get_addvote_action( const name& voter, const asset& vote_staked ) {
      return get_action( config::system_account_name,
                         "addvote"_n,
                         get_active_perms(voter),
                         mutable_variant_object()
                              ("voter",         voter)
                              ("vote_staked",   vote_staked )
      );
   }

   action_result subvote( const account_name& voter, const asset& vote_staked ) {
      return push_action(voter, "subvote"_n, mvo()
                         ("voter",     voter)
                         ("vote_staked", vote_staked));
   }

   action_result subvote( const account_name& voter, const asset& vote_staked1, const asset& vote_staked2 ) {
      return subvote(voter, vote_staked1 + vote_staked2);
   }

   action_result subvote( const string& voter, const asset& vote_staked1, const asset& vote_staked2 ) {
      return subvote(account_name(voter), vote_staked1, vote_staked2);
   }

   action_result voterefund( const name& voter ) {
      return push_action(voter, "voterefund"_n, mvo()("owner", voter));
   }

   action_result cfgelection() {
      auto start_time = control->pending_block_time();
      return push_action(config::system_account_name, "cfgelection"_n, mvo()
            ("election_activated_time", start_time)
            ("reward_started_time", start_time)
            ("initial_rewards_per_block", core_sym::from_string("0.8000")) );
   }

   uint32_t last_block_time() const {
      return time_point_sec( control->head().block_time() ).sec_since_epoch();
   }

   asset get_balance( const account_name& act, symbol balance_symbol = symbol{CORE_SYM} ) {
      vector<char> data = get_row_by_account( "flon.token"_n, act, "accounts"_n, account_name(balance_symbol.to_symbol_code().value) );
      return data.empty() ? asset(0, balance_symbol) : token_abi_ser.binary_to_variant("account", data, abi_serializer::create_yield_function(abi_serializer_max_time))["balance"].as<asset>();
   }

   asset get_balance( std::string_view act, symbol balance_symbol = symbol{CORE_SYM} ) {
      return get_balance( account_name(act), balance_symbol );
   }

   fc::variant get_total_stake( const account_name& act ) {
      vector<char> data = get_row_by_account( config::system_account_name, act, "userres"_n, act );
      return data.empty() ? fc::variant() : abi_ser.binary_to_variant( "user_resources", data, abi_serializer::create_yield_function(abi_serializer_max_time) );
   }
   fc::variant get_total_stake(  std::string_view act ) {
      return get_total_stake( account_name(act) );
   }

   fc::variant get_voter_info( const account_name& act ) {
      vector<char> data = get_row_by_account( config::system_account_name, config::system_account_name, "voters"_n, act );
      return data.empty() ? fc::variant() : abi_ser.binary_to_variant( "voter_info", data, abi_serializer::create_yield_function(abi_serializer_max_time) );
   }
   fc::variant get_voter_info(  std::string_view act ) {
      return get_voter_info( account_name(act) );
   }

   fc::variant get_producer_info( const account_name& act ) {
      vector<char> data = get_row_by_account( config::system_account_name, config::system_account_name, "producers"_n, act );
      return abi_ser.binary_to_variant( "producer_info", data, abi_serializer::create_yield_function(abi_serializer_max_time) );
   }
   fc::variant get_producer_info( std::string_view act ) {
      return get_producer_info( account_name(act) );
   }

   fc::variant get_producer_info2( const account_name& act ) {
      vector<char> data = get_row_by_account( config::system_account_name, config::system_account_name, "producers2"_n, act );
      return abi_ser.binary_to_variant( "producer_info2", data, abi_serializer::create_yield_function(abi_serializer_max_time) );
   }
   fc::variant get_producer_info2( std::string_view act ) {
      return get_producer_info2( account_name(act) );
   }

   void create_currency( name contract, name manager, asset maxsupply ) {
      auto act =  mutable_variant_object()
         ("issuer",       manager )
         ("maximum_supply", maxsupply );

      base_tester::push_action(contract, "create"_n, contract, act );
   }

   void issue( const asset& quantity, const name& to = config::system_account_name ) {
      base_tester::push_action( "flon.token"_n, "issue"_n, to, mutable_variant_object()
                                ("to",       to )
                                ("quantity", quantity )
                                ("memo",     "")
                                );
   }

   void retire( const asset& quantity, const name& issuer = config::system_account_name ) {
      base_tester::push_action( "flon.token"_n, "retire"_n, issuer, mutable_variant_object()
                                ("quantity", quantity )
                                ("memo",     "")
                                );
   }

   void issuefixed( const asset& supply, const name& to = config::system_account_name ) {
      base_tester::push_action( "flon.token"_n, "issuefixed"_n, to, mutable_variant_object()
                                ("to",       to )
                                ("supply", supply )
                                ("memo",     "")
                                );
   }

   void setmaxsupply( const asset& maximum_supply, const name& issuer = config::system_account_name) {
      base_tester::push_action( "flon.token"_n, "setmaxsupply"_n, issuer, mutable_variant_object()
                                ("issuer",       issuer )
                                ("maximum_supply", maximum_supply )
                                );
   }

   void transfer( const name& from, const name& to, const asset& amount, const name& manager = config::system_account_name ) {
      base_tester::push_action( "flon.token"_n, "transfer"_n, manager, mutable_variant_object()
                                ("from",    from)
                                ("to",      to )
                                ("quantity", amount)
                                ("memo", "")
                                );
   }

   void transfer( const name& from, std::string_view to, const asset& amount, const name& manager = config::system_account_name ) {
      transfer( from, name(to), amount, manager );
   }

   void transfer( std::string_view from, std::string_view to, const asset& amount, std::string_view manager ) {
      transfer( name(from), name(to), amount, name(manager) );
   }

   void transfer( std::string_view from, std::string_view to, const asset& amount ) {
      transfer( name(from), name(to), amount );
   }

   void issue_and_transfer( const name& to, const asset& amount, const name& manager = config::system_account_name ) {
      signed_transaction trx;
      trx.actions.emplace_back( get_action( "flon.token"_n, "issue"_n,
                                            vector<permission_level>{{manager, config::active_name}},
                                            mutable_variant_object()
                                            ("to",       manager )
                                            ("quantity", amount )
                                            ("memo",     "")
                                            )
                                );
      if ( to != manager ) {
         trx.actions.emplace_back( get_action( "flon.token"_n, "transfer"_n,
                                               vector<permission_level>{{manager, config::active_name}},
                                               mutable_variant_object()
                                               ("from",     manager)
                                               ("to",       to )
                                               ("quantity", amount )
                                               ("memo",     "")
                                               )
                                   );
      }
      set_transaction_headers( trx );
      trx.sign( get_private_key( manager, "active" ), control->get_chain_id()  );
      push_transaction( trx );
   }

   void issue_and_transfer( std::string_view to, const asset& amount, std::string_view manager ) {
      issue_and_transfer( name(to), amount, name(manager) );
   }

   void issue_and_transfer( std::string_view to, const asset& amount, const name& manager ) {
      issue_and_transfer( name(to), amount, manager);
   }

   void issue_and_transfer( std::string_view to, const asset& amount ) {
      issue_and_transfer( name(to), amount );
   }

   int64_t stake2votes( asset stake ) {
      return stake.get_amount();
   }

   int64_t stake2votes( const string& s ) {
      return stake2votes( core_sym::from_string(s) );
   }

   fc::variant get_stats( const string& symbolname ) {
      auto symb = eosio::chain::symbol::from_string(symbolname);
      auto symbol_code = symb.to_symbol_code().value;
      vector<char> data = get_row_by_account( "flon.token"_n, name(symbol_code), "stat"_n, account_name(symbol_code) );
      return data.empty() ? fc::variant() : token_abi_ser.binary_to_variant( "currency_stats", data, abi_serializer::create_yield_function(abi_serializer_max_time) );
   }

   asset get_token_supply() {
      return get_stats("4," CORE_SYM_NAME)["supply"].as<asset>();
   }

   uint64_t microseconds_since_epoch_of_iso_string( const fc::variant& v ) {
      return static_cast<uint64_t>( time_point::from_iso_string( v.as_string() ).time_since_epoch().count() );
   }

   fc::variant get_global_state() {
      vector<char> data = get_row_by_account( config::system_account_name, config::system_account_name, "global"_n, "global"_n );
      if (data.empty()) std::cout << "\nData is empty\n" << std::endl;
      return data.empty() ? fc::variant() : abi_ser.binary_to_variant( "eosio_global_state", data, abi_serializer::create_yield_function(abi_serializer_max_time) );
   }

   fc::variant get_global_state2() {
      vector<char> data = get_row_by_account( config::system_account_name, config::system_account_name, "global2"_n, "global2"_n );
      return data.empty() ? fc::variant() : abi_ser.binary_to_variant( "eosio_global_state2", data, abi_serializer::create_yield_function(abi_serializer_max_time) );
   }

   fc::variant get_global_state3() {
      vector<char> data = get_row_by_account( config::system_account_name, config::system_account_name, "global3"_n, "global3"_n );
      return data.empty() ? fc::variant() : abi_ser.binary_to_variant( "eosio_global_state3", data, abi_serializer::create_yield_function(abi_serializer_max_time) );
   }

   fc::variant get_refund_request( name account ) {
      vector<char> data = get_row_by_account( config::system_account_name, account, "refunds"_n, account );
      return data.empty() ? fc::variant() : abi_ser.binary_to_variant( "refund_request", data, abi_serializer::create_yield_function(abi_serializer_max_time) );
   }

   abi_serializer initialize_multisig() {
      abi_serializer msig_abi_ser;
      {
         create_account_with_resources( "flon.msig"_n, config::system_account_name );
         BOOST_REQUIRE_EQUAL( success(), buyram( "flon"_n, "flon.msig"_n, core_sym::from_string("5000.0000") ) );
         produce_block();

         auto trace = base_tester::push_action(config::system_account_name, "setpriv"_n,
                                               config::system_account_name,  mutable_variant_object()
                                               ("account", "flon.msig")
                                               ("is_priv", 1)
         );

         set_code( "flon.msig"_n, contracts::msig_wasm() );
         set_abi( "flon.msig"_n, contracts::msig_abi().data() );

         produce_blocks();
         const auto& accnt = control->db().get<account_object,by_name>( "flon.msig"_n );
         abi_def msig_abi;
         BOOST_REQUIRE_EQUAL(abi_serializer::to_abi(accnt.abi, msig_abi), true);
         msig_abi_ser.set_abi(msig_abi, abi_serializer::create_yield_function(abi_serializer_max_time));
      }
      return msig_abi_ser;
   }

   vector<name> active_and_vote_producers(int32_t num_producers = 21) {
      //stake more than 15% of total EOS supply to activate chain
      transfer( "flon"_n, "alice1111111"_n, core_sym::from_string("650000000.0000"), config::system_account_name );
      BOOST_REQUIRE_EQUAL( success(), addvote( "alice1111111"_n, "alice1111111"_n, core_sym::from_string("300000000.0000"), core_sym::from_string("300000000.0000") ) );

      // create accounts {defproducera, defproducerb, ..., defproducerz} and register as producers
      std::vector<account_name> producer_names;
      {
         producer_names.reserve('z' - 'a' + 1);
         const std::string root("defproducer");
         for ( char c = 'a'; c < 'a'+num_producers; ++c ) {
            producer_names.emplace_back(root + std::string(1, c));
         }
         setup_producer_accounts(producer_names);
         for (const auto& p: producer_names) {

            BOOST_REQUIRE_EQUAL( success(), regproducer(p) );
         }
      }
      produce_block();
      produce_block(fc::seconds(1000));

      auto trace_auth = validating_tester::push_action(config::system_account_name, updateauth::get_name(), config::system_account_name, mvo()
                                            ("account", name(config::system_account_name).to_string())
                                            ("permission", name(config::active_name).to_string())
                                            ("parent", name(config::owner_name).to_string())
                                            ("auth",  authority(1, {key_weight{get_public_key( config::system_account_name, "active" ), 1}}, {
                                                  permission_level_weight{{config::system_account_name, config::eosio_code_name}, 1},
                                                     permission_level_weight{{config::producers_account_name,  config::active_name}, 1}
                                               }
                                            ))
      );
      BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace_auth->receipt->status);

      //vote for producers
      {
         transfer( config::system_account_name, "alice1111111"_n, core_sym::from_string("100000000.0000"), config::system_account_name );
         BOOST_REQUIRE_EQUAL(success(), addvote( "alice1111111"_n, core_sym::from_string("60000000.0000")) );
         BOOST_REQUIRE_EQUAL(success(), buyram( "alice1111111"_n, "alice1111111"_n, core_sym::from_string("30000000.0000") ) );
         BOOST_REQUIRE_EQUAL(success(), push_action("alice1111111"_n, "voteproducer"_n, mvo()
                                                    ("voter",  "alice1111111")
                                                    ("proxy", name(0).to_string())
                                                    ("producers", vector<account_name>(producer_names.begin(), producer_names.begin()+num_producers))
                             )
         );
      }
      cfgelection();
      // produce_blocks( 2 * 21 ); // This is minimum number of blocks required by ram_gift in system_tests
      produce_blocks( 250 );
      auto producer_schedule = control->active_producers();
      BOOST_REQUIRE_EQUAL( 21u, producer_schedule.producers.size() );
      BOOST_REQUIRE_EQUAL( name("defproducera"), producer_schedule.producers[0].producer_name );

      return producer_names;
   }

   void cross_15_percent_threshold() {
      setup_producer_accounts({"producer1111"_n});
      regproducer("producer1111"_n);
      cfgelection();
      {
         signed_transaction trx;
         set_transaction_headers(trx);
         trx.actions.emplace_back( get_action( "flon.token"_n, "transfer"_n,
                                               vector<permission_level>{{config::system_account_name, config::active_name}},
                                               mutable_variant_object()
                                               ("from",     config::system_account_name)
                                               ("to",       "producer1111"_n )
                                               ("quantity", core_sym::from_string("150000000.0000") )
                                               ("memo",     "")
                                             )
                                 );
         trx.actions.emplace_back( get_action( config::system_account_name, "addvote"_n,
                                               vector<permission_level>{{"producer1111"_n, config::active_name}},
                                               mvo()
                                               ("voter", "producer1111"_n)
                                               ("vote_staked", core_sym::from_string("150000000.0000") )
                                             )
                                 );
         trx.actions.emplace_back( get_action( config::system_account_name, "voteproducer"_n,
                                               vector<permission_level>{{"producer1111"_n, config::active_name}},
                                               mvo()
                                               ("voter", "producer1111")
                                               ("proxy", name(0).to_string())
                                               ("producers", vector<account_name>(1, "producer1111"_n))
                                             )
                                 );
         trx.actions.emplace_back( get_action( config::system_account_name, "subvote"_n,
                                               vector<permission_level>{{"producer1111"_n, config::active_name}},
                                               mvo()
                                               ("voter", "producer1111")
                                               ("vote_staked", core_sym::from_string("150000000.0000") )
                                             )
                                 );

         set_transaction_headers(trx);
         trx.sign( get_private_key( config::system_account_name, "active" ), control->get_chain_id()  );
         trx.sign( get_private_key( "producer1111"_n, "active" ), control->get_chain_id()  );
         push_transaction( trx );
         produce_block();
         produce_block(fc::days(3));
         voterefund("producer1111"_n);
         transfer( "producer1111"_n, "flon"_n, core_sym::from_string("150000000.0000"), "producer1111"_n);
         produce_block();
      }
   }

   abi_serializer abi_ser;
   abi_serializer token_abi_ser;
};

inline fc::mutable_variant_object voter( account_name acct ) {
   return mutable_variant_object()
      ("owner", acct)
      ("proxy", name(0).to_string())
      ("producers", variants() )
      ("staked", int64_t(0))
      //("last_vote_weight", double(0))
      ("proxied_vote_weight", double(0))
      ("is_proxy", 0)
      ;
}
inline fc::mutable_variant_object voter( std::string_view acct ) {
   return voter( account_name(acct) );
}

inline fc::mutable_variant_object voter( account_name acct, const asset& vote_stake ) {
   return voter( acct )( "votes", vote_stake.get_amount() );
}
inline fc::mutable_variant_object voter( std::string_view acct, const asset& vote_stake ) {
   return voter( account_name(acct), vote_stake );
}

inline fc::mutable_variant_object voter( account_name acct, int64_t vote_stake ) {
   return voter( acct )( "votes", vote_stake );
}
inline fc::mutable_variant_object voter( std::string_view acct, int64_t vote_stake ) {
   return voter( account_name(acct), vote_stake );
}

inline fc::mutable_variant_object proxy( account_name acct ) {
   return voter( acct )( "is_proxy", 1 );
}

inline uint64_t M( const string& eos_str ) {
   return core_sym::from_string( eos_str ).get_amount();
}

}

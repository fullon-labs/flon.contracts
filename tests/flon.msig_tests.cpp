#include <boost/test/unit_test.hpp>
#include <eosio/testing/tester.hpp>
#include <eosio/chain/abi_serializer.hpp>
#include <eosio/chain/global_property_object.hpp>
#include <eosio/chain/wast_to_wasm.hpp>

#include <fc/variant_object.hpp>
#include "contracts.hpp"
#include "test_symbol.hpp"

using namespace eosio::testing;
using namespace eosio;
using namespace eosio::chain;
using namespace eosio::testing;
using namespace fc;

using mvo = fc::mutable_variant_object;

class eosio_msig_tester : public tester {
public:
   eosio_msig_tester() {
      create_accounts( { "flon.msig"_n, "flon.stake"_n, "flon.ram"_n, "flon.fees"_n, "alice"_n, "bob"_n, "carol"_n ,
               "flon.reward"_n, "flon.vote"_n } );
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
      abi_def abi;
      BOOST_REQUIRE_EQUAL(abi_serializer::to_abi(accnt.abi, abi), true);
      abi_ser.set_abi(abi, abi_serializer::create_yield_function(abi_serializer_max_time));
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

      trx.actions.emplace_back( get_action( "flon"_n, "buyram"_n, vector<permission_level>{{creator,config::active_name}},
                                            mvo()
                                            ("payer", creator)
                                            ("receiver", a)
                                            ("quant", ramfunds) )
                              );

      trx.actions.emplace_back( get_action( "flon"_n, "delegatebw"_n, vector<permission_level>{{creator,config::active_name}},
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
   void create_currency( name contract, name manager, asset maxsupply ) {
      auto act =  mutable_variant_object()
         ("issuer",       manager )
         ("maximum_supply", maxsupply );

      base_tester::push_action(contract, "create"_n, contract, act );
   }
   void issue( name to, const asset& amount, name manager = config::system_account_name ) {
      base_tester::push_action( "flon.token"_n, "issue"_n, manager, mutable_variant_object()
                                ("to",      to )
                                ("quantity", amount )
                                ("memo", "")
                                );
   }
   void transfer( name from, name to, const string& amount, name manager = config::system_account_name ) {
      base_tester::push_action( "flon.token"_n, "transfer"_n, manager, mutable_variant_object()
                                ("from",    from)
                                ("to",      to )
                                ("quantity", asset::from_string(amount) )
                                ("memo", "")
                                );
   }
   asset get_balance( const account_name& act ) {
      //return get_currency_balance( config::system_account_name, symbol(CORE_SYMBOL), act );
      //temporary code. current get_currency_balancy uses table name "accounts"_n from currency.h
      //generic_currency table name is "account"_n.
      const auto& db  = control->db();
      const auto* tbl = db.find<table_id_object, by_code_scope_table>(boost::make_tuple("flon.token"_n, act, "accounts"_n));
      share_type result = 0;

      // the balance is implied to be 0 if either the table or row does not exist
      if (tbl) {
         const auto *obj = db.find<key_value_object, by_scope_primary>(boost::make_tuple(tbl->id, symbol(CORE_SYM).to_symbol_code()));
         if (obj) {
            // balance is the first field in the serialization
            fc::datastream<const char *> ds(obj->value.data(), obj->value.size());
            fc::raw::unpack(ds, result);
         }
      }
      return asset( result, symbol(CORE_SYM) );
   }

   transaction_trace_ptr push_action( const account_name& signer, const action_name& name, const variant_object& data, bool auth = true ) {
      vector<account_name> accounts;
      if( auth )
         accounts.push_back( signer );
      auto trace = base_tester::push_action( "flon.msig"_n, name, accounts, data );
      produce_block();
      BOOST_REQUIRE_EQUAL( true, chain_has_transaction(trace->id) );
      return trace;

      /*
         string action_type_name = abi_ser.get_action_type(name);

         action act;
         act.account = "flon.msig"_n;
         act.name = name;
         act.data = abi_ser.variant_to_binary( action_type_name, data, abi_serializer::create_yield_function(abi_serializer_max_time) );
         //std::cout << "test:\n" << fc::to_hex(act.data.data(), act.data.size()) << " size = " << act.data.size() << std::endl;

         return base_tester::push_action( std::move(act), auth ? uint64_t(signer) : 0 );
      */
   }

   transaction reqauth( account_name from, const vector<permission_level>& auths, const fc::microseconds& max_serialization_time );

   void check_traces(transaction_trace_ptr trace, std::vector<std::map<std::string, name>> res);

   abi_serializer abi_ser;
};

transaction eosio_msig_tester::reqauth( account_name from, const vector<permission_level>& auths, const fc::microseconds& max_serialization_time ) {
   fc::variants v;
   for ( auto& level : auths ) {
      v.push_back(fc::mutable_variant_object()
                  ("actor", level.actor)
                  ("permission", level.permission)
      );
   }
   fc::variant pretty_trx = fc::mutable_variant_object()
      ("expiration", "2020-01-01T00:30")
      ("ref_block_num", 2)
      ("ref_block_prefix", 3)
      ("max_net_usage_words", 0)
      ("max_cpu_usage_ms", 0)
      ("delay_sec", 0)
      ("actions", fc::variants({
            fc::mutable_variant_object()
               ("account", name(config::system_account_name))
               ("name", "reqauth")
               ("authorization", v)
               ("data", fc::mutable_variant_object() ("from", from) )
               })
      );
   transaction trx;
   abi_serializer::from_variant(pretty_trx, trx, get_resolver(), abi_serializer::create_yield_function(max_serialization_time));
   return trx;
}

void eosio_msig_tester::check_traces(transaction_trace_ptr trace, std::vector<std::map<std::string, name>> res) {

   BOOST_REQUIRE( bool(trace) );
   BOOST_REQUIRE_EQUAL( transaction_receipt::executed, trace->receipt->status );
   BOOST_REQUIRE_EQUAL( res.size(), trace->action_traces.size() );

   for (size_t i = 0; i < res.size(); i++) {
      auto cur_action = trace->action_traces.at(i);
      BOOST_REQUIRE_EQUAL( res[i]["receiver"], cur_action.receiver );
      BOOST_REQUIRE_EQUAL( res[i]["act_name"], cur_action.act.name );
   }
}

BOOST_AUTO_TEST_SUITE(eosio_msig_tests)

BOOST_FIXTURE_TEST_CASE( propose_approve_execute, eosio_msig_tester ) try {
   auto trx = reqauth( "alice"_n, {permission_level{"alice"_n, config::active_name}}, abi_serializer_max_time );

   push_action( "alice"_n, "propose"_n, mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx)
                  ("requested", vector<permission_level>{{ "alice"_n, config::active_name }})
   );

   //fail to execute before approval
   BOOST_REQUIRE_EXCEPTION( push_action( "alice"_n, "exec"_n, mvo()
                                          ("proposer",      "alice")
                                          ("proposal_name", "first")
                                          ("executer",      "alice")
                            ),
                            eosio_assert_message_exception,
                            eosio_assert_message_is("transaction authorization failed")
   );

   //approve and execute
   push_action( "alice"_n, "approve"_n, mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ "alice"_n, config::active_name })
   );

   transaction_trace_ptr trace = push_action( "alice"_n, "exec"_n, mvo()
                                             ("proposer",      "alice")
                                             ("proposal_name", "first")
                                             ("executer",      "alice")
   );

   check_traces( trace, {
                        {{"receiver", "flon.msig"_n}, {"act_name", "exec"_n}},
                        {{"receiver", config::system_account_name}, {"act_name", "reqauth"_n}}
                        } );
} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( propose_approve_unapprove, eosio_msig_tester ) try {
   auto trx = reqauth( "alice"_n, {permission_level{"alice"_n, config::active_name}}, abi_serializer_max_time );

   push_action( "alice"_n, "propose"_n, mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx)
                  ("requested", vector<permission_level>{{ "alice"_n, config::active_name }})
   );

   push_action( "alice"_n, "approve"_n, mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ "alice"_n, config::active_name })
   );

   push_action( "alice"_n, "unapprove"_n, mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ "alice"_n, config::active_name })
   );

   BOOST_REQUIRE_EXCEPTION( push_action( "alice"_n, "exec"_n, mvo()
                                          ("proposer",      "alice")
                                          ("proposal_name", "first")
                                          ("executer",      "alice")
                            ),
                            eosio_assert_message_exception,
                            eosio_assert_message_is("transaction authorization failed")
   );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( propose_approve_by_two, eosio_msig_tester ) try {
   auto trx = reqauth( "alice"_n, vector<permission_level>{ { "alice"_n, config::active_name }, { "bob"_n, config::active_name } }, abi_serializer_max_time );
   push_action( "alice"_n, "propose"_n, mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx)
                  ("requested", vector<permission_level>{ { "alice"_n, config::active_name }, { "bob"_n, config::active_name } })
   );

   //approve by alice
   push_action( "alice"_n, "approve"_n, mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ "alice"_n, config::active_name })
   );

   //fail because approval by bob is missing

   BOOST_REQUIRE_EXCEPTION( push_action( "alice"_n, "exec"_n, mvo()
                                          ("proposer",      "alice")
                                          ("proposal_name", "first")
                                          ("executer",      "alice")
                            ),
                            eosio_assert_message_exception,
                            eosio_assert_message_is("transaction authorization failed")
   );

   //approve by bob and execute
   push_action( "bob"_n, "approve"_n, mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ "bob"_n, config::active_name })
   );

   transaction_trace_ptr trace = push_action( "alice"_n, "exec"_n, mvo()
                                            ("proposer",      "alice")
                                            ("proposal_name", "first")
                                            ("executer",      "alice")
   );

   check_traces( trace, {
                     {{"receiver", "flon.msig"_n}, {"act_name", "exec"_n}},
                     {{"receiver", config::system_account_name}, {"act_name", "reqauth"_n}}
                     } );
} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( propose_with_wrong_requested_auth, eosio_msig_tester ) try {
   auto trx = reqauth( "alice"_n, vector<permission_level>{ { "alice"_n, config::active_name },  { "bob"_n, config::active_name } }, abi_serializer_max_time );
   //try with not enough requested auth
   BOOST_REQUIRE_EXCEPTION( push_action( "alice"_n, "propose"_n, mvo()
                                             ("proposer",      "alice")
                                             ("proposal_name", "third")
                                             ("trx",           trx)
                                             ("requested", vector<permission_level>{ { "alice"_n, config::active_name } } )
                            ),
                            eosio_assert_message_exception,
                            eosio_assert_message_is("transaction authorization failed")
   );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( big_transaction, eosio_msig_tester ) try {
   //change `default_max_inline_action_size` to 512 KB
   eosio::chain::chain_config params = control->get_global_properties().configuration;
   params.max_inline_action_size = 512 * 1024;
   base_tester::push_action( config::system_account_name, "setparams"_n, config::system_account_name, mutable_variant_object()
                              ("params", params) );

   produce_blocks();

   vector<permission_level> perm = { { "alice"_n, config::active_name }, { "bob"_n, config::active_name } };
   auto wasm = contracts::util::exchange_wasm();

   fc::variant pretty_trx = fc::mutable_variant_object()
      ("expiration", "2020-01-01T00:30")
      ("ref_block_num", 2)
      ("ref_block_prefix", 3)
      ("max_net_usage_words", 0)
      ("max_cpu_usage_ms", 0)
      ("delay_sec", 0)
      ("actions", fc::variants({
            fc::mutable_variant_object()
               ("account", name(config::system_account_name))
               ("name", "setcode")
               ("authorization", perm)
               ("data", fc::mutable_variant_object()
                ("account", "alice")
                ("vmtype", 0)
                ("vmversion", 0)
                ("code", bytes( wasm.begin(), wasm.end() ))
               )
               })
      );

   transaction trx;
   abi_serializer::from_variant(pretty_trx, trx, get_resolver(), abi_serializer::create_yield_function(abi_serializer_max_time));

   push_action( "alice"_n, "propose"_n, mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx)
                  ("requested", perm)
   );

   //approve by alice
   push_action( "alice"_n, "approve"_n, mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ "alice"_n, config::active_name })
   );
   //approve by bob and execute
   push_action( "bob"_n, "approve"_n, mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ "bob"_n, config::active_name })
   );

   transaction_trace_ptr trace = push_action( "alice"_n, "exec"_n, mvo()
                                            ("proposer",      "alice")
                                            ("proposal_name", "first")
                                            ("executer",      "alice")
   );

   check_traces( trace, {
                        {{"receiver", "flon.msig"_n}, {"act_name", "exec"_n}},
                        {{"receiver", config::system_account_name}, {"act_name", "setcode"_n}}
                        } );
} FC_LOG_AND_RETHROW()



BOOST_FIXTURE_TEST_CASE( update_system_contract_all_approve, eosio_msig_tester ) try {

   // required to set up the link between (eosio active) and (flon.prods active)
   //
   //                  eosio active
   //                       |
   //             flon.prods active (2/3 threshold)
   //             /         |        \             <--- implicitly updated in onblock action
   // alice active     bob active   carol active

   set_authority(
      config::system_account_name,
      config::active_name,
      authority( 1,
                 vector<key_weight>{{get_private_key(config::system_account_name, "active").get_public_key(), 1}},
                 vector<permission_level_weight>{{{"flon.prods"_n, config::active_name}, 1}}
      ),
      config::owner_name,
      {{config::system_account_name, config::active_name}},
      {get_private_key(config::system_account_name, "active")}
   );

   set_producers( {"alice"_n,"bob"_n,"carol"_n} );
   produce_blocks(50);

   set_code( "flon.reward"_n, contracts::reward_wasm());
   set_abi( "flon.reward"_n, contracts::reward_abi().data() );

   create_accounts( { "flon.token"_n } );
   set_code( "flon.token"_n, contracts::token_wasm() );
   set_abi( "flon.token"_n, contracts::token_abi().data() );

   create_currency( "flon.token"_n, config::system_account_name, core_sym::from_string("10000000000.0000") );
   issue(config::system_account_name, core_sym::from_string("1000000000.0000"));
   BOOST_REQUIRE_EQUAL( core_sym::from_string("1000000000.0000"),
                        get_balance(config::system_account_name) + get_balance("flon.stake"_n) + get_balance("flon.ram"_n) );

   set_code( config::system_account_name, contracts::system_wasm() );
   set_abi( config::system_account_name, contracts::system_abi().data() );
   base_tester::push_action( config::system_account_name, "init"_n,
                             config::system_account_name,  mutable_variant_object()
                              ("version", 0)
                              ("core", CORE_SYM_STR)
   );
   produce_blocks();
   create_account_with_resources( "alice1111111"_n, "flon"_n, core_sym::from_string("1.0000"), false );
   create_account_with_resources( "bob111111111"_n, "flon"_n, core_sym::from_string("0.4500"), false );
   create_account_with_resources( "carol1111111"_n, "flon"_n, core_sym::from_string("1.0000"), false );

   BOOST_REQUIRE_EQUAL( core_sym::from_string("1000000000.0000"),
                        get_balance(config::system_account_name) + get_balance("flon.stake"_n)
                        + get_balance("flon.ram"_n) + get_balance("flon.fees"_n) );

   vector<permission_level> perm = { { "alice"_n, config::active_name }, { "bob"_n, config::active_name },
      {"carol"_n, config::active_name} };

   vector<permission_level> action_perm = {{"flon"_n, config::active_name}};

   auto wasm = contracts::util::reject_all_wasm();

   fc::variant pretty_trx = fc::mutable_variant_object()
      ("expiration", "2020-01-01T00:30")
      ("ref_block_num", 2)
      ("ref_block_prefix", 3)
      ("max_net_usage_words", 0)
      ("max_cpu_usage_ms", 0)
      ("delay_sec", 0)
      ("actions", fc::variants({
            fc::mutable_variant_object()
               ("account", name(config::system_account_name))
               ("name", "setcode")
               ("authorization", action_perm)
               ("data", fc::mutable_variant_object()
                ("account", name(config::system_account_name))
                ("vmtype", 0)
                ("vmversion", 0)
                ("code", bytes( wasm.begin(), wasm.end() ))
               )
               })
      );

   transaction trx;
   abi_serializer::from_variant(pretty_trx, trx, get_resolver(), abi_serializer::create_yield_function(abi_serializer_max_time));

   // propose action
   push_action( "alice"_n, "propose"_n, mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx)
                  ("requested", perm)
   );

   //approve by alice
   push_action( "alice"_n, "approve"_n, mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ "alice"_n, config::active_name })
   );
   //approve by bob
   push_action( "bob"_n, "approve"_n, mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ "bob"_n, config::active_name })
   );
   //approve by carol
   push_action( "carol"_n, "approve"_n, mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ "carol"_n, config::active_name })
   );

   // execute by alice to replace the eosio system contract
   transaction_trace_ptr trace = push_action( "alice"_n, "exec"_n, mvo()
                                             ("proposer",      "alice")
                                             ("proposal_name", "first")
                                             ("executer",      "alice")
   );

   check_traces( trace, {
                        {{"receiver", "flon.msig"_n}, {"act_name", "exec"_n}},
                        {{"receiver", config::system_account_name}, {"act_name", "setcode"_n}}
                        } );

   // can't create account because system contract was replaced by the reject_all contract
   BOOST_REQUIRE_EXCEPTION( create_account_with_resources( "alice1111112"_n, "flon"_n, core_sym::from_string("1.0000"), false ),
                            eosio_assert_message_exception, eosio_assert_message_is("rejecting all actions")

   );
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( update_system_contract_major_approve, eosio_msig_tester ) try {

   // set up the link between (eosio active) and (flon.prods active)
   set_authority(
      config::system_account_name,
      config::active_name,
      authority( 1,
                 vector<key_weight>{{get_private_key(config::system_account_name, "active").get_public_key(), 1}},
                 vector<permission_level_weight>{{{"flon.prods"_n, config::active_name}, 1}}
      ),
      config::owner_name,
      {{config::system_account_name, config::active_name}},
      {get_private_key(config::system_account_name, "active")}
   );

   create_accounts( { "apple"_n } );
   set_producers( {"alice"_n,"bob"_n,"carol"_n, "apple"_n} );
   produce_blocks(50);

   set_code( "flon.reward"_n, contracts::reward_wasm());
   set_abi( "flon.reward"_n, contracts::reward_abi().data() );

   create_accounts( { "flon.token"_n} );
   set_code( "flon.token"_n, contracts::token_wasm() );
   set_abi( "flon.token"_n, contracts::token_abi().data() );

   create_currency( "flon.token"_n, config::system_account_name, core_sym::from_string("10000000000.0000") );
   issue(config::system_account_name, core_sym::from_string("1000000000.0000"));
   BOOST_REQUIRE_EQUAL( core_sym::from_string("1000000000.0000"), get_balance( config::system_account_name ) );

   set_code( config::system_account_name, contracts::system_wasm() );
   set_abi( config::system_account_name, contracts::system_abi().data() );
   base_tester::push_action( config::system_account_name, "init"_n,
                             config::system_account_name,  mutable_variant_object()
                                 ("version", 0)
                                 ("core", CORE_SYM_STR)
   );
   produce_blocks();

   create_account_with_resources( "alice1111111"_n, "flon"_n, core_sym::from_string("1.0000"), false );
   create_account_with_resources( "bob111111111"_n, "flon"_n, core_sym::from_string("0.4500"), false );
   create_account_with_resources( "carol1111111"_n, "flon"_n, core_sym::from_string("1.0000"), false );

   BOOST_REQUIRE_EQUAL( core_sym::from_string("1000000000.0000"),
                        get_balance(config::system_account_name) + get_balance("flon.stake"_n)
                        + get_balance("flon.ram"_n) + get_balance("flon.fees"_n) );

   vector<permission_level> perm = { { "alice"_n, config::active_name }, { "bob"_n, config::active_name },
      {"carol"_n, config::active_name}, {"apple"_n, config::active_name}};

   vector<permission_level> action_perm = {{"flon"_n, config::active_name}};

   auto wasm = contracts::util::reject_all_wasm();

   fc::variant pretty_trx = fc::mutable_variant_object()
      ("expiration", "2020-01-01T00:30")
      ("ref_block_num", 2)
      ("ref_block_prefix", 3)
      ("max_net_usage_words", 0)
      ("max_cpu_usage_ms", 0)
      ("delay_sec", 0)
      ("actions", fc::variants({
            fc::mutable_variant_object()
               ("account", name(config::system_account_name))
               ("name", "setcode")
               ("authorization", action_perm)
               ("data", fc::mutable_variant_object()
                ("account", name(config::system_account_name))
                ("vmtype", 0)
                ("vmversion", 0)
                ("code", bytes( wasm.begin(), wasm.end() ))
               )
               })
      );

   transaction trx;
   abi_serializer::from_variant(pretty_trx, trx, get_resolver(), abi_serializer::create_yield_function(abi_serializer_max_time));

   // propose action
   push_action( "alice"_n, "propose"_n, mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx)
                  ("requested", perm)
   );

   //approve by alice
   push_action( "alice"_n, "approve"_n, mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ "alice"_n, config::active_name })
   );
   //approve by bob
   push_action( "bob"_n, "approve"_n, mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ "bob"_n, config::active_name })
   );

   // not enough approvers
   BOOST_REQUIRE_EXCEPTION(
      push_action( "alice"_n, "exec"_n, mvo()
                     ("proposer",      "alice")
                     ("proposal_name", "first")
                     ("executer",      "alice")
      ),
      eosio_assert_message_exception, eosio_assert_message_is("transaction authorization failed")
   );

   //approve by apple
   push_action( "apple"_n, "approve"_n, mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ "apple"_n, config::active_name })
   );
   // execute by another producer different from proposer
   transaction_trace_ptr trace = push_action( "apple"_n, "exec"_n, mvo()
                                             ("proposer",      "alice")
                                             ("proposal_name", "first")
                                             ("executer",      "apple")
   );

   check_traces( trace, {
                        {{"receiver", "flon.msig"_n}, {"act_name", "exec"_n}},
                        {{"receiver", config::system_account_name}, {"act_name", "setcode"_n}}
                        } );

   // can't create account because system contract was replaced by the reject_all contract
   BOOST_REQUIRE_EXCEPTION( create_account_with_resources( "alice1111112"_n, "flon"_n, core_sym::from_string("1.0000"), false ),
                            eosio_assert_message_exception, eosio_assert_message_is("rejecting all actions")

   );
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( propose_approve_invalidate, eosio_msig_tester ) try {
   auto trx = reqauth( "alice"_n, {permission_level{"alice"_n, config::active_name}}, abi_serializer_max_time );

   push_action( "alice"_n, "propose"_n, mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx)
                  ("requested", vector<permission_level>{{ "alice"_n, config::active_name }})
   );

   //fail to execute before approval
   BOOST_REQUIRE_EXCEPTION( push_action( "alice"_n, "exec"_n, mvo()
                                          ("proposer",      "alice")
                                          ("proposal_name", "first")
                                          ("executer",      "alice")
                            ),
                            eosio_assert_message_exception,
                            eosio_assert_message_is("transaction authorization failed")
   );

   //approve
   push_action( "alice"_n, "approve"_n, mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ "alice"_n, config::active_name })
   );

   //invalidate
   push_action( "alice"_n, "invalidate"_n, mvo()
                  ("account",      "alice")
   );

   //fail to execute after invalidation
   BOOST_REQUIRE_EXCEPTION( push_action( "alice"_n, "exec"_n, mvo()
                                          ("proposer",      "alice")
                                          ("proposal_name", "first")
                                          ("executer",      "alice")
                            ),
                            eosio_assert_message_exception,
                            eosio_assert_message_is("transaction authorization failed")
   );
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( propose_invalidate_approve, eosio_msig_tester ) try {
   auto trx = reqauth( "alice"_n, {permission_level{"alice"_n, config::active_name}}, abi_serializer_max_time );

   push_action( "alice"_n, "propose"_n, mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx)
                  ("requested", vector<permission_level>{{ "alice"_n, config::active_name }})
   );

   //fail to execute before approval
   BOOST_REQUIRE_EXCEPTION( push_action( "alice"_n, "exec"_n, mvo()
                                          ("proposer",      "alice")
                                          ("proposal_name", "first")
                                          ("executer",      "alice")
                            ),
                            eosio_assert_message_exception,
                            eosio_assert_message_is("transaction authorization failed")
   );

   //invalidate
   push_action( "alice"_n, "invalidate"_n, mvo()
                  ("account",      "alice")
   );

   //approve
   push_action( "alice"_n, "approve"_n, mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ "alice"_n, config::active_name })
   );

   transaction_trace_ptr trace = push_action( "bob"_n, "exec"_n, mvo()
                                            ("proposer",      "alice")
                                            ("proposal_name", "first")
                                            ("executer",      "bob")
   );

   check_traces( trace, {
                        {{"receiver", "flon.msig"_n}, {"act_name", "exec"_n}},
                        {{"receiver", config::system_account_name}, {"act_name", "reqauth"_n}}
                        } );
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( approve_with_hash, eosio_msig_tester ) try {
   auto trx = reqauth( "alice"_n, {permission_level{"alice"_n, config::active_name}}, abi_serializer_max_time );
   auto trx_hash = fc::sha256::hash( trx );
   auto not_trx_hash = fc::sha256::hash( trx_hash );

   push_action( "alice"_n, "propose"_n, mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx)
                  ("requested", vector<permission_level>{{ "alice"_n, config::active_name }})
   );

   //fail to approve with incorrect hash
   BOOST_REQUIRE_EXCEPTION( push_action( "alice"_n, "approve"_n, mvo()
                                          ("proposer",      "alice")
                                          ("proposal_name", "first")
                                          ("level",         permission_level{ "alice"_n, config::active_name })
                                          ("proposal_hash", not_trx_hash)
                            ),
                            eosio::chain::crypto_api_exception,
                            fc_exception_message_is("hash mismatch")
   );

   //approve and execute
   push_action( "alice"_n, "approve"_n, mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("level",         permission_level{ "alice"_n, config::active_name })
                  ("proposal_hash", trx_hash)
   );

   transaction_trace_ptr trace = push_action( "alice"_n, "exec"_n, mvo()
                                            ("proposer",      "alice")
                                            ("proposal_name", "first")
                                            ("executer",      "alice")
   );
   check_traces( trace, {
                        {{"receiver", "flon.msig"_n}, {"act_name", "exec"_n}},
                        {{"receiver", config::system_account_name}, {"act_name", "reqauth"_n}}
                        } );
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( switch_proposal_and_fail_approve_with_hash, eosio_msig_tester ) try {
   auto trx1 = reqauth( "alice"_n, {permission_level{"alice"_n, config::active_name}}, abi_serializer_max_time );
   auto trx1_hash = fc::sha256::hash( trx1 );

   push_action( "alice"_n, "propose"_n, mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx1)
                  ("requested", vector<permission_level>{{ "alice"_n, config::active_name }})
   );

   auto trx2 = reqauth( "alice"_n,
                       { permission_level{"alice"_n, config::active_name},
                         permission_level{"alice"_n, config::owner_name}  },
                       abi_serializer_max_time );

   push_action( "alice"_n, "cancel"_n, mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("canceler",       "alice")
   );

   push_action( "alice"_n, "propose"_n, mvo()
                  ("proposer",      "alice")
                  ("proposal_name", "first")
                  ("trx",           trx2)
                  ("requested", vector<permission_level>{ { "alice"_n, config::active_name },
                                                          { "alice"_n, config::owner_name } })
   );

   //fail to approve with hash meant for old proposal
   BOOST_REQUIRE_EXCEPTION( push_action( "alice"_n, "approve"_n, mvo()
                                          ("proposer",      "alice")
                                          ("proposal_name", "first")
                                          ("level",         permission_level{ "alice"_n, config::active_name })
                                          ("proposal_hash", trx1_hash)
                            ),
                            eosio::chain::crypto_api_exception,
                            fc_exception_message_is("hash mismatch")
   );
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( sendinline, eosio_msig_tester ) try {
   create_accounts( {"sendinline"_n} );
   set_code( "sendinline"_n, system_contracts::testing::test_contracts::sendinline_wasm() );
   set_abi( "sendinline"_n, system_contracts::testing::test_contracts::sendinline_abi().data() );

   create_accounts( {"wrongcon"_n} );
   set_code( "wrongcon"_n, system_contracts::testing::test_contracts::sendinline_wasm() );
   set_abi( "wrongcon"_n, system_contracts::testing::test_contracts::sendinline_abi().data() );
   produce_blocks();

   action act = get_action( config::system_account_name, "reqauth"_n, {}, mvo()("from", "alice"));

   BOOST_REQUIRE_EXCEPTION( base_tester::push_action( "sendinline"_n, "send"_n, "bob"_n, mvo()
                                                       ("contract", "flon")
                                                       ("action_name", "reqauth")
                                                       ("auths", std::vector<permission_level>{ {"alice"_n, config::active_name} })
                                                       ("payload", act.data)
                          ),
                          unsatisfied_authorization,
                          fc_exception_message_starts_with("transaction declares authority")
   );

   base_tester::push_action(config::system_account_name, "updateauth"_n, "alice"_n, mvo()
                              ("account", "alice")
                              ("permission", "perm")
                              ("parent", "active")
                              ("auth",  authority{ 1, {}, {permission_level_weight{ {"sendinline"_n, config::active_name}, 1}}, {} })
   );
   produce_blocks();

   base_tester::push_action( config::system_account_name, "linkauth"_n, "alice"_n, mvo()
                              ("account", "alice")
                              ("code", "flon")
                              ("type", "reqauth")
                              ("requirement", "perm")
   );
   produce_blocks();

   transaction_trace_ptr trace = base_tester::push_action( "sendinline"_n, "send"_n, "bob"_n, mvo()
                                                            ("contract", "flon")
                                                            ("action_name", "reqauth")
                                                            ("auths", std::vector<permission_level>{ {"alice"_n, "perm"_n} })
                                                            ("payload", act.data)
   );
   check_traces( trace, {
                        {{"receiver", "sendinline"_n}, {"act_name", "send"_n}},
                        {{"receiver", config::system_account_name}, {"act_name", "reqauth"_n}}
                        } );

   produce_blocks();

   action approve_act = get_action("flon.msig"_n, "approve"_n, {}, mvo()
                                    ("proposer", "bob")
                                    ("proposal_name", "first")
                                    ("level", permission_level{"sendinline"_n, config::active_name})
   );

   transaction trx = reqauth( "alice"_n, {permission_level{"alice"_n, "perm"_n}}, abi_serializer_max_time );

   base_tester::push_action( "flon.msig"_n, "propose"_n, "bob"_n, mvo()
                              ("proposer", "bob")
                              ("proposal_name", "first")
                              ("trx", trx)
                              ("requested", std::vector<permission_level>{{ "sendinline"_n, config::active_name}})
   );
   produce_blocks();

   base_tester::push_action( "sendinline"_n, "send"_n, "bob"_n, mvo()
                              ("contract", "flon.msig")
                              ("action_name", "approve")
                              ("auths", std::vector<permission_level>{{"sendinline"_n, config::active_name}})
                              ("payload", approve_act.data)
   );
   produce_blocks();

   trace = base_tester::push_action( "flon.msig"_n, "exec"_n, "bob"_n, mvo()
                                          ("proposer", "bob")
                                          ("proposal_name", "first")
                                          ("executer", "bob")
   );

   check_traces( trace, {
                        {{"receiver", "flon.msig"_n}, {"act_name", "exec"_n}},
                        {{"receiver", config::system_account_name}, {"act_name", "reqauth"_n}}
                        } );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
#include <boost/test/unit_test.hpp>
#include <eosio/chain/contract_table_objects.hpp>
#include <eosio/chain/global_property_object.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/wast_to_wasm.hpp>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <fc/log/logger.hpp>
#include <eosio/chain/exceptions.hpp>

#include "flon.system_tester.hpp"
struct _abi_hash {
   name owner;
   fc::sha256 hash;
};
FC_REFLECT( _abi_hash, (owner)(hash) );

struct connector {
   asset balance;
   double weight = .5;
};
FC_REFLECT( connector, (balance)(weight) );

using namespace eosio_system;

bool within_error(int64_t a, int64_t b, int64_t err) { return std::abs(a - b) <= err; };
bool within_one(int64_t a, int64_t b) { return within_error(a, b, 1); }

// Split the tests into multiple suites so that they can run in parallel in CICD to
// reduce overall CICD time..
// Each suite is grouped by functionality and takes approximately the same amount of time.

BOOST_AUTO_TEST_SUITE(eosio_system_resource_tests)

// Resource limits are denominated in Gas. CPU/NET delegation and refunds
// were removed from the protocol; BP self stake is covered in the producer suite.
BOOST_FIXTURE_TEST_CASE(set_gas_limit, eosio_system_tester) try {
   uint64_t before = 0;
   bool unlimited = false;
   control->get_resource_limits_manager().get_account_limits("alice1111111"_n, before, unlimited);
   BOOST_REQUIRE(!unlimited);
   BOOST_REQUIRE_EQUAL(success(), push_action("flon"_n, "setalimits"_n,
      mvo()("account", "alice1111111")("gas", uint64_t{5000000})("is_unlimited", false)));
   uint64_t after = 0;
   control->get_resource_limits_manager().get_account_limits("alice1111111"_n, after, unlimited);
   BOOST_REQUIRE_EQUAL(uint64_t{5000000}, after);
   BOOST_REQUIRE(!unlimited);
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(set_gas_limit_requires_system_auth, eosio_system_tester) try {
   BOOST_REQUIRE_NE(success(), push_action("alice1111111"_n, "setalimits"_n,
      mvo()("account", "alice1111111")("gas", uint64_t{5000000})("is_unlimited", false)));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(set_gas_limit_rejects_no_change, eosio_system_tester) try {
   uint64_t gas = 0;
   bool unlimited = false;
   control->get_resource_limits_manager().get_account_limits("alice1111111"_n, gas, unlimited);
   BOOST_REQUIRE_EQUAL(wasm_assert_msg("data does not have change"),
      push_action("flon"_n, "setalimits"_n,
         mvo()("account", "alice1111111")("gas", gas)("is_unlimited", unlimited)));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(set_gas_limit_rejects_missing_account, eosio_system_tester) try {
   BOOST_REQUIRE_EQUAL(wasm_assert_msg("account not exists"),
      push_action("flon"_n, "setalimits"_n,
         mvo()("account", "missingacct1")("gas", uint64_t{5000000})("is_unlimited", false)));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(buy_gas_rejects_wrong_symbol, eosio_system_tester) try {
   BOOST_REQUIRE_EQUAL(wasm_assert_msg("must buy gas with core asset"),
      push_action("alice1111111"_n, "buygasself"_n,
         mvo()("account", "alice1111111")("quant", "1.0000 TEST")));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(buy_gas_rejects_system_gas_account, eosio_system_tester) try {
   transfer("flon"_n, "alice1111111"_n, core_sym::from_string("10.0000"));
   BOOST_REQUIRE_EQUAL(wasm_assert_msg("neither payer nor receiver can be system gas account"),
      push_action("alice1111111"_n, "buygas"_n,
         mvo()("payer", "alice1111111")("receiver", "flon.gas")("quant", "1.0000 TST")));
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
BOOST_AUTO_TEST_SUITE(eosio_system_producer_tests)

BOOST_AUTO_TEST_CASE( account_setup_uses_current_gas_limits ) try {
   eosio_system_tester test;
   BOOST_REQUIRE_NO_THROW(test.create_account_with_resources("dave11111111"_n,
      config::system_account_name, uint32_t{8000}));
   BOOST_REQUIRE_NO_THROW(test.setup_producer_accounts({"defproducera"_n}));
   for (const auto account : {"alice1111111"_n, "dave11111111"_n, "defproducera"_n}) {
      uint64_t gas = 0;
      bool unlimited = true;
      test.control->get_resource_limits_manager().get_account_limits(account, gas, unlimited);
      BOOST_REQUIRE_GT(gas, uint64_t{0});
      BOOST_REQUIRE_LE(gas, uint64_t{1000000000});
      BOOST_REQUIRE(!unlimited);
   }
}
FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE( idle_block_interval_matches_core_limit ) try {
   eosio_system_tester test(eosio_system_tester::setup_level::deploy_contract);
   BOOST_REQUIRE_EQUAL(test.success(), test.push_action(config::system_account_name, "setibintervl"_n,
      mvo()("idle_block_interval_ms", uint64_t{3600 * 1000})));
   BOOST_REQUIRE_EQUAL(test.wasm_assert_msg("idle block interval must not exceed one hour"),
      test.push_action(config::system_account_name, "setibintervl"_n,
         mvo()("idle_block_interval_ms", uint64_t{3600 * 1000 + 1})));
}
FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( producer_self_stake_qualification_and_refund, eosio_system_tester ) try {
   const name producer = "alice1111111"_n;
   const auto minimum = core_sym::from_string("500.0000");
   issue_and_transfer(producer, core_sym::from_string("1000.0000"), config::system_account_name);

   BOOST_REQUIRE_EQUAL(success(), push_action(config::system_account_name, "setbpstake"_n, mvo()
      ("enforce_schedule", false)
      ("min_self_stake", minimum)
      ("unstake_delay_sec", 24 * 60 * 60)));

   BOOST_REQUIRE_EQUAL(wasm_assert_msg("producer self-stake is below the minimum"),
      push_action(producer, "regproducer"_n, mvo()
         ("producer", producer)
         ("producer_key", get_public_key(producer, "active"))
         ("url", "")
         ("location", 0)
         ("reward_shared_ratio", 0)));

   BOOST_REQUIRE_EQUAL(success(), push_action(producer, "stakebp"_n, mvo()
      ("owner", producer)("quantity", core_sym::from_string("499.0000"))));
   BOOST_REQUIRE_EQUAL(wasm_assert_msg("producer self-stake is below the minimum"),
      push_action(producer, "regproducer"_n, mvo()
         ("producer", producer)
         ("producer_key", get_public_key(producer, "active"))
         ("url", "")
         ("location", 0)
         ("reward_shared_ratio", 0)));

   BOOST_REQUIRE_EQUAL(success(), push_action(producer, "stakebp"_n, mvo()
      ("owner", producer)("quantity", core_sym::from_string("1.0000"))));
   BOOST_REQUIRE_EQUAL(success(), regproducer(producer));

   BOOST_REQUIRE_EQUAL(wasm_assert_msg("registered producer must retain the minimum self-stake"),
      push_action(producer, "unstakebp"_n, mvo()
         ("owner", producer)("quantity", core_sym::from_string("1.0000"))));

   BOOST_REQUIRE_EQUAL(success(), push_action(producer, "unregprod"_n, mvo()("producer", producer)));
   BOOST_REQUIRE_EQUAL(success(), push_action(producer, "unstakebp"_n, mvo()
      ("owner", producer)("quantity", minimum)));
   BOOST_REQUIRE_EQUAL(wasm_assert_msg("BP self-stake refund is not available yet"),
      push_action(producer, "claimbpstake"_n, mvo()("owner", producer)));

   produce_block(fc::days(1));
   BOOST_REQUIRE_EQUAL(success(), push_action(producer, "claimbpstake"_n, mvo()("owner", producer)));
   BOOST_REQUIRE_EQUAL(core_sym::from_string("1000.0000"), get_balance(producer));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( uncommitted_bp_self_stake_can_be_redeemed_immediately, eosio_system_tester ) try {
   const name applicant = "alice1111111"_n;
   const auto stake = core_sym::from_string("500.0000");
   issue_and_transfer(applicant, core_sym::from_string("1000.0000"), config::system_account_name);

   BOOST_REQUIRE_EQUAL(success(), push_action(config::system_account_name, "setbpstake"_n, mvo()
      ("enforce_schedule", false)
      ("min_self_stake", stake)
      ("unstake_delay_sec", 24 * 60 * 60)));
   BOOST_REQUIRE_EQUAL(success(), push_action(applicant, "stakebp"_n, mvo()
      ("owner", applicant)("quantity", stake)));
   BOOST_REQUIRE_EQUAL(core_sym::from_string("500.0000"), get_balance(applicant));

   // A candidate deposit that has never entered a registered producer
   // lifecycle is not governance power and can be returned immediately.
   BOOST_REQUIRE_EQUAL(success(), push_action(applicant, "unstakebp"_n, mvo()
      ("owner", applicant)("quantity", stake)));
   BOOST_REQUIRE_EQUAL(core_sym::from_string("1000.0000"), get_balance(applicant));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( committed_bp_self_stake_refund_blocks_restake_until_settled, eosio_system_tester ) try {
   const name producer = "alice1111111"_n;
   const auto stake = core_sym::from_string("500.0000");
   issue_and_transfer(producer, core_sym::from_string("1100.0000"), config::system_account_name);

   BOOST_REQUIRE_EQUAL(success(), push_action(config::system_account_name, "setbpstake"_n, mvo()
      ("enforce_schedule", false)
      ("min_self_stake", stake)
      ("unstake_delay_sec", 24 * 60 * 60)));
   BOOST_REQUIRE_EQUAL(success(), push_action(producer, "stakebp"_n, mvo()
      ("owner", producer)("quantity", stake)));
   BOOST_REQUIRE_EQUAL(success(), regproducer(producer));
   BOOST_REQUIRE_EQUAL(success(), push_action(producer, "unregprod"_n, mvo()("producer", producer)));
   BOOST_REQUIRE_EQUAL(success(), push_action(producer, "unstakebp"_n, mvo()
      ("owner", producer)("quantity", stake)));

   BOOST_REQUIRE_EQUAL(wasm_assert_msg("pending BP self-stake refund must be claimed or cancelled first"),
      push_action(producer, "stakebp"_n, mvo()
         ("owner", producer)("quantity", core_sym::from_string("1.0000"))));

   // Cancelling restores the same committed lifecycle; withdrawing it again
   // therefore still follows the configured delay.
   BOOST_REQUIRE_EQUAL(success(), push_action(producer, "cancelbpref"_n, mvo()("owner", producer)));
   BOOST_REQUIRE_EQUAL(success(), push_action(producer, "unstakebp"_n, mvo()
      ("owner", producer)("quantity", stake)));
   produce_block(fc::days(1));
   BOOST_REQUIRE_EQUAL(success(), push_action(producer, "claimbpstake"_n, mvo()("owner", producer)));

   // Once that generation is fully settled, a new pre-registration deposit
   // starts a fresh, uncommitted generation and is immediately redeemable.
   BOOST_REQUIRE_EQUAL(success(), push_action(producer, "stakebp"_n, mvo()
      ("owner", producer)("quantity", core_sym::from_string("100.0000"))));
   BOOST_REQUIRE_EQUAL(success(), push_action(producer, "unstakebp"_n, mvo()
      ("owner", producer)("quantity", core_sym::from_string("100.0000"))));
   BOOST_REQUIRE_EQUAL(core_sym::from_string("1100.0000"), get_balance(producer));
} FC_LOG_AND_RETHROW()

// Tests for voting
BOOST_FIXTURE_TEST_CASE( producer_configuration_uses_fixed_reward_schedule, eosio_system_tester ) try {
   BOOST_REQUIRE_EQUAL(success(), setprodcfg());

   const auto global = get_global_state();
   BOOST_REQUIRE_EQUAL(core_sym::from_string("0.0000"),
                       global["initial_rewards_per_block"].as<asset>());

   const auto data = get_row_by_account(config::system_account_name,
                                        config::system_account_name,
                                        "rewardplans"_n, name{});
   BOOST_REQUIRE(!data.empty());
   const auto plan = abi_ser.binary_to_variant(
      "reward_epoch_plan", data, abi_serializer::create_yield_function(abi_serializer_max_time));
   BOOST_REQUIRE_EQUAL(0u, plan["first_epoch_id"].as_uint64());
   BOOST_REQUIRE_EQUAL(7200u, plan["epoch_slots"].as_uint64());
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( producer_configuration_rejects_legacy_reward_override, eosio_system_tester ) try {
   const auto start_time = control->pending_block_time();
   BOOST_REQUIRE_EQUAL(
      wasm_assert_msg("initial block reward is fixed by protocol; compatibility field must be zero"),
      push_action(config::system_account_name, "setprodcfg"_n, mvo()
         ("schedule_start", start_time)
         ("reward_epoch_start", start_time)
         ("initial_block_reward", core_sym::from_string("0.8000"))));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( epoch_boundary_materializes_pending_voter_state, eosio_system_tester ) try {
   const name producer = "producer1111"_n;
   const name voter = "alice1111111"_n;
   setup_producer_accounts({producer});
   BOOST_REQUIRE_EQUAL(success(), regproducer(producer));
   issue_and_transfer(voter, core_sym::from_string("100.0000"), config::system_account_name);
   BOOST_REQUIRE_EQUAL(success(), setprodcfg());

   BOOST_REQUIRE_EQUAL(success(), addvote(voter, core_sym::from_string("100.0000")));
   BOOST_REQUIRE_EQUAL(success(), vote(voter, {producer}));
   BOOST_REQUIRE(!get_row_by_account(config::system_account_name,
                                     config::system_account_name,
                                     "pendingvote"_n, voter).empty());
   BOOST_REQUIRE(!get_row_by_account("voter.reward"_n, "voter.reward"_n,
                                     "pendingvote"_n, voter).empty());

   // Cross the one-hour reward boundary and allow bounded onblock sealing to
   // finish. Both contracts must materialize the voter without requiring a
   // later vote or claim action from that account.
   produce_block(fc::hours(2));
   produce_blocks(100);

   BOOST_REQUIRE(get_row_by_account(config::system_account_name,
                                    config::system_account_name,
                                    "pendingvote"_n, voter).empty());
   BOOST_REQUIRE(get_row_by_account("voter.reward"_n, "voter.reward"_n,
                                    "pendingvote"_n, voter).empty());
   const auto voter_info = get_voter_info(voter);
   BOOST_REQUIRE(!voter_info.is_null());
   BOOST_REQUIRE_EQUAL(core_sym::from_string("100.0000").get_amount(),
                       voter_info["votes"].as_int64());
   const auto producer_info = get_producer_info(producer);
   BOOST_REQUIRE_EQUAL(core_sym::from_string("100.0000").get_amount(),
                       producer_info["total_votes"].as_int64());
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( producer_register_unregister, eosio_system_tester ) try {
   issue_and_transfer( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );

   //fc::variant params = producer_parameters_example(1);
   auto key =  fc::crypto::public_key( std::string("EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV") ); // cspell:disable-line
   BOOST_REQUIRE_EQUAL( success(), push_action("alice1111111"_n, "regproducer"_n, mvo()
                                               ("producer",  "alice1111111")
                                               ("producer_key", key )
                                               ("url", "http://block.one")
                                               ("location", 1)
                                               ("status_url", "https://status.block.one")
                        )
   );

   auto info = get_producer_info( "alice1111111" );
   BOOST_REQUIRE_EQUAL( "alice1111111", info["owner"].as_string() );
   BOOST_REQUIRE_EQUAL( 0, info["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( "http://block.one", info["url"].as_string() );
   BOOST_REQUIRE_EQUAL( "https://status.block.one", info["status_url"].as_string() );

   //change parameters one by one to check for things like #3783
   //fc::variant params2 = producer_parameters_example(2);
   BOOST_REQUIRE_EQUAL( success(), push_action("alice1111111"_n, "regproducer"_n, mvo()
                                               ("producer",  "alice1111111")
                                               ("producer_key", key )
                                               ("url", "http://block.two")
                                               ("location", 1)
                                               ("status_url", "https://status.block.two")
                        )
   );
   info = get_producer_info( "alice1111111" );
   BOOST_REQUIRE_EQUAL( "alice1111111", info["owner"].as_string() );
   BOOST_REQUIRE_EQUAL( key, fc::crypto::public_key(info["producer_key"].as_string()) );
   BOOST_REQUIRE_EQUAL( "http://block.two", info["url"].as_string() );
   BOOST_REQUIRE_EQUAL( "https://status.block.two", info["status_url"].as_string() );
   BOOST_REQUIRE_EQUAL( 1, info["location"].as_int64() );

   auto key2 =  fc::crypto::public_key( std::string("EOS5jnmSKrzdBHE9n8hw58y7yxFWBC8SNiG7m8S1crJH3KvAnf9o6") ); // cspell:disable-line
   BOOST_REQUIRE_EQUAL( success(), push_action("alice1111111"_n, "regproducer"_n, mvo()
                                               ("producer",  "alice1111111")
                                               ("producer_key", key2 )
                                               ("url", "http://block.two")
                                               ("location", 2)
                        )
   );
   info = get_producer_info( "alice1111111" );
   BOOST_REQUIRE_EQUAL( "alice1111111", info["owner"].as_string() );
   BOOST_REQUIRE_EQUAL( key2, fc::crypto::public_key(info["producer_key"].as_string()) );
   BOOST_REQUIRE_EQUAL( "http://block.two", info["url"].as_string() );
   BOOST_REQUIRE_EQUAL( 2, info["location"].as_int64() );

   //unregister producer
   BOOST_REQUIRE_EQUAL( success(), push_action("alice1111111"_n, "unregprod"_n, mvo()
                                               ("producer",  "alice1111111")
                        )
   );
   info = get_producer_info( "alice1111111" );
   //key should be empty
   BOOST_REQUIRE_EQUAL( fc::crypto::public_key(), fc::crypto::public_key(info["producer_key"].as_string()) );
   //everything else should stay the same
   BOOST_REQUIRE_EQUAL( "alice1111111", info["owner"].as_string() );
   BOOST_REQUIRE_EQUAL( 0, info["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( "http://block.two", info["url"].as_string() );

   //unregister bob111111111 who is not a producer
   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "producer not found" ),
                        push_action( "bob111111111"_n, "unregprod"_n, mvo()
                                     ("producer",  "bob111111111")
                        )
   );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( producer_wtmsig, eosio_system_tester ) try {
   block_signing_authority_v0 alice_signing_authority;
   alice_signing_authority.threshold = 1;
   alice_signing_authority.keys.push_back( {.key = get_public_key( "alice1111111"_n, "bs1"), .weight = 1} );
   alice_signing_authority.keys.push_back( {.key = get_public_key( "alice1111111"_n, "bs2"), .weight = 1} );
   producer_authority alice_producer_authority = {.producer_name = "alice1111111"_n, .authority = alice_signing_authority};
   BOOST_REQUIRE_EQUAL( success(), push_action( "alice1111111"_n, "regproducer2"_n, mvo()
                                               ("producer",  "alice1111111")
                                               ("producer_authority", alice_producer_authority.get_abi_variant()["authority"])
                                               ("url", "http://alice1111111.flon.io")
                                               ("location", 0 )
                        )
   );
   auto alice_prod_info = get_producer_info( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( alice_prod_info["is_active"], true );

   alice_signing_authority.threshold = 0;
   alice_producer_authority.authority = alice_signing_authority;

   // Ensure an authority with a threshold of 0 is rejected.
   BOOST_REQUIRE_EQUAL( error("assertion failure with message: invalid producer authority"),
                        push_action( "alice1111111"_n, "regproducer2"_n, mvo()
                                       ("producer",  "alice1111111")
                                       ("producer_authority", alice_producer_authority.get_abi_variant()["authority"])
                                       ("url", "http://block.one")
                                       ("location", 0 )
                        )
   );

   // Ensure an authority that is not satisfiable is rejected.
   alice_signing_authority.threshold = 3;
   alice_producer_authority.authority = alice_signing_authority;
   BOOST_REQUIRE_EQUAL( error("assertion failure with message: invalid producer authority"),
                        push_action( "alice1111111"_n, "regproducer2"_n, mvo()
                                       ("producer",  "alice1111111")
                                       ("producer_authority", alice_producer_authority.get_abi_variant()["authority"])
                                       ("url", "http://block.one")
                                       ("location", 0 )
                        )
   );

   // Ensure an authority with duplicate keys is rejected.
   alice_signing_authority.threshold = 1;
   alice_signing_authority.keys[1] = alice_signing_authority.keys[0];
   alice_producer_authority.authority = alice_signing_authority;
   BOOST_REQUIRE_EQUAL( error("assertion failure with message: invalid producer authority"),
                        push_action( "alice1111111"_n, "regproducer2"_n, mvo()
                                       ("producer",  "alice1111111")
                                       ("producer_authority", alice_producer_authority.get_abi_variant()["authority"])
                                       ("url", "http://block.one")
                                       ("location", 0 )
                        )
   );

   // However, an authority with an invalid key is okay.
   alice_signing_authority.keys[1] = {};
   alice_producer_authority.authority = alice_signing_authority;
   BOOST_REQUIRE_EQUAL( success(),
                        push_action( "alice1111111"_n, "regproducer2"_n, mvo()
                                       ("producer",  "alice1111111")
                                       ("producer_authority", alice_producer_authority.get_abi_variant()["authority"])
                                       ("url", "http://block.one")
                                       ("location", 0 )
                        )
   );

   BOOST_REQUIRE_EQUAL(get_producer_info("alice1111111"_n)["is_active"], true);

} FC_LOG_AND_RETHROW()

// BOOST_FIXTURE_TEST_CASE( producer_wtmsig_transition, eosio_system_tester ) try {
//    activate_election();

//    BOOST_REQUIRE_EQUAL( control->active_producers().version, 0u );

//    // TODO: not support system_wasm_v1_8
//    // set_code( config::system_account_name, contracts::util::system_wasm_v1_8() );
//    // set_abi(  config::system_account_name, contracts::util::system_abi_v1_8().data() );

//    // issue_and_transfer( "alice1111111"_n, core_sym::from_string("200000000.0000"),  config::system_account_name );
//    // BOOST_REQUIRE_EQUAL( success(), push_action( "alice1111111"_n, "regproducer"_n, mvo()
//    //                                             ("producer",  "alice1111111")
//    //                                             ("producer_key", get_public_key( "alice1111111"_n, "active") )
//    //                                             ("url","")
//    //                                             ("location", 0)
//    //                      )
//    // );
//    // BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111"_n, core_sym::from_string("100000000.0000"), core_sym::from_string("100000000.0000") ) );
//    // BOOST_REQUIRE_EQUAL( success(), vote( "alice1111111"_n, { "alice1111111"_n } ) );

//    // //auto alice_prod_info1 = get_producer_info( "alice1111111"_n );
//    // //wdump((alice_prod_info1));

//    // produce_block();
//    // produce_block( fc::minutes(2) );
//    // produce_blocks(config::producer_repetitions * 2);
//    // BOOST_REQUIRE_EQUAL( control->active_producers().version, 1u );

//    auto convert_to_block_timestamp = [](const fc::variant& timestamp) -> eosio::chain::block_timestamp_type {
//       return fc::time_point::from_iso_string(timestamp.as_string());
//    };

//    // const auto schedule_update1 = convert_to_block_timestamp(get_global_state()["last_producer_schedule_update"]);

//    // const auto& rlm = control->get_resource_limits_manager();

//    // auto alice_initial_ram_usage = rlm.get_account_ram_usage("alice1111111"_n);

//    set_code( config::system_account_name, contracts::system_wasm() );
//    set_abi(  config::system_account_name, contracts::system_abi().data() );
//    produce_block();
//    BOOST_REQUIRE_EQUAL( control->pending_block_producer(), "alice1111111"_n );

//    auto alice_prod_info2 = get_producer_info( "alice1111111"_n );
//    BOOST_REQUIRE_EQUAL( alice_prod_info2["is_active"], true );

//    // produce_block( fc::minutes(2) );
//    const auto schedule_update2 = convert_to_block_timestamp(get_global_state()["last_producer_schedule_update"]);
//    // BOOST_REQUIRE( schedule_update1 < schedule_update2 ); // Ensure last_producer_schedule_update is increasing.

//    // Producing the above block would trigger the bug in v1.9.0 that sets the default block_signing_authority
//    // in the producer object of the currently active producer alice1111111.
//    // However, starting in v1.9.1, the producer object does not have a default block_signing_authority added to the
//    // serialization of the producer object if it was not already present in the binary extension field
//    // producer_authority to begin with. This is verified below by ensuring the RAM usage of alice (who pays for the
//    // producer object) does not increase.

//    // auto alice_ram_usage = rlm.get_account_ram_usage("alice1111111"_n);
//    // BOOST_CHECK_EQUAL( alice_initial_ram_usage, alice_ram_usage );

//    auto alice_prod_info3 = get_producer_info( "alice1111111"_n );
//    if( alice_prod_info3.get_object().contains("producer_authority") ) {
//       BOOST_CHECK_EQUAL( alice_prod_info3["producer_authority"][1]["threshold"], 0 );
//    }

//    produce_block( fc::minutes(2) );
//    const auto schedule_update3 = convert_to_block_timestamp(get_global_state()["last_producer_schedule_update"]);

//    // The bug in v1.9.0 would cause alice to have an invalid producer authority (the default block_signing_authority).
//    // The v1.9.0 system contract would have attempted to set a proposed producer schedule including this invalid
//    // authority which would be rejected by the EOSIO native system and cause the onblock transaction to continue to fail.
//    // This could be observed by noticing that last_producer_schedule_update was not being updated even though it should.
//    // However, starting in v1.9.1, update_elected_producers is smarter about the producer schedule it constructs to
//    // propose to the system. It will recognize the default constructed authority (which shouldn't be created by the
//    // v1.9.1 system contract but may still exist in the tables if it was constructed by the buggy v1.9.0 system contract)
//    // and instead resort to constructing the block signing authority from the single producer key in the table.
//    // So newer system contracts should see onblock continue to function, which is verified by the check below.

//    BOOST_CHECK( schedule_update2 < schedule_update3 ); // Ensure last_producer_schedule_update is increasing.

//    // But even if the buggy v1.9.0 system contract was running, it should always still be possible to recover
//    // by having the producer with the invalid authority simply call regproducer or regproducer2 to correct their
//    // block signing authority.

//    BOOST_REQUIRE_EQUAL( success(), push_action( "alice1111111"_n, "regproducer"_n, mvo()
//                                                ("producer",  "alice1111111")
//                                                ("producer_key", get_public_key( "alice1111111"_n, "active") )
//                                                ("url","")
//                                                ("location", 0)
//                         )
//    );

//    produce_block();
//    produce_block( fc::minutes(2) );

//    auto alice_prod_info4 = get_producer_info( "alice1111111"_n );
//    BOOST_REQUIRE_EQUAL( alice_prod_info4["is_active"], true );
//    const auto schedule_update4 = convert_to_block_timestamp(get_global_state()["last_producer_schedule_update"]);
//    BOOST_REQUIRE( schedule_update2 < schedule_update4 );

// } FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(vote_and_unvote_at_epoch_boundary, eosio_system_tester) try {
   const name producer = "producer1111"_n;
   const name voter_name = "alice1111111"_n;
   setup_producer_accounts({producer});
   BOOST_REQUIRE_EQUAL(success(), regproducer(producer));
   issue_and_transfer(voter_name, core_sym::from_string("100.0000"), config::system_account_name);
   activate_election();
   BOOST_REQUIRE_EQUAL(success(), addvote(voter_name, core_sym::from_string("100.0000")));
   BOOST_REQUIRE_EQUAL(success(), vote(voter_name, {producer}));
   produce_block(fc::hours(2));
   produce_blocks(100);
   BOOST_REQUIRE_EQUAL(core_sym::from_string("100.0000").get_amount(),
                       get_producer_info(producer)["total_votes"].as_int64());
   BOOST_REQUIRE_EQUAL(success(), subvote(voter_name, core_sym::from_string("20.0000")));
   produce_block(fc::hours(2));
   produce_blocks(100);
   BOOST_REQUIRE_EQUAL(core_sym::from_string("80.0000").get_amount(),
                       get_producer_info(producer)["total_votes"].as_int64());
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( unregistered_producer_voting, eosio_system_tester, * boost::unit_test::tolerance(1e+5) ) try {
   issue_and_transfer( "bob111111111", core_sym::from_string("2000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), addvote( "bob111111111", core_sym::from_string("13.0000"), core_sym::from_string("0.5791") ) );
   REQUIRE_MATCHING_OBJECT( voter( "bob111111111", core_sym::from_string("13.5791") ), get_voter_info( "bob111111111" ) );

   //bob111111111 should not be able to vote for alice1111111 who is not a producer
   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "producer alice1111111 is not registered" ),
                        vote( "bob111111111"_n, { "alice1111111"_n } ) );

   //alice1111111 registers as a producer
   issue_and_transfer( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   fc::variant params = producer_parameters_example(1);
   BOOST_REQUIRE_EQUAL( success(), push_action( "alice1111111"_n, "regproducer"_n, mvo()
                                               ("producer",  "alice1111111")
                                               ("producer_key", get_public_key( "alice1111111"_n, "active") )
                                               ("url", "")
                                               ("location", 0)
                        )
   );
   //and then unregisters
   BOOST_REQUIRE_EQUAL( success(), push_action( "alice1111111"_n, "unregprod"_n, mvo()
                                               ("producer",  "alice1111111")
                        )
   );
   //key should be empty
   auto prod = get_producer_info( "alice1111111" );
   BOOST_REQUIRE_EQUAL( fc::crypto::public_key(), fc::crypto::public_key(prod["producer_key"].as_string()) );

   //bob111111111 should not be able to vote for alice1111111 who is not an active producer
   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "producer alice1111111 is not active" ),
                        vote( "bob111111111"_n, { "alice1111111"_n } ) );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( more_than_30_producer_voting, eosio_system_tester ) try {
   issue_and_transfer( "bob111111111", core_sym::from_string("2000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), addvote( "bob111111111", core_sym::from_string("13.0000"), core_sym::from_string("0.5791") ) );
   REQUIRE_MATCHING_OBJECT( voter( "bob111111111", core_sym::from_string("13.5791") ), get_voter_info( "bob111111111" ) );

   //bob111111111 should not be able to vote for alice1111111 who is not a producer
   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "attempt to vote for too many producers" ),
                        vote( "bob111111111"_n, vector<account_name>(31, "alice1111111"_n) ) );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( vote_same_producer_30_times, eosio_system_tester ) try {
   issue_and_transfer( "bob111111111", core_sym::from_string("2000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), addvote( "bob111111111", core_sym::from_string("50.0000"), core_sym::from_string("50.0000") ) );
   REQUIRE_MATCHING_OBJECT( voter( "bob111111111", core_sym::from_string("100.0000") ), get_voter_info( "bob111111111" ) );

   //alice1111111 becomes a producer
   issue_and_transfer( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   fc::variant params = producer_parameters_example(1);
   BOOST_REQUIRE_EQUAL( success(), push_action( "alice1111111"_n, "regproducer"_n, mvo()
                                               ("producer",  "alice1111111")
                                               ("producer_key", get_public_key("alice1111111"_n, "active") )
                                               ("url", "")
                                               ("location", 0)
                        )
   );

   //bob111111111 should not be able to vote for alice1111111 who is not a producer
   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "producer votes must be unique and sorted" ),
                        vote( "bob111111111"_n, vector<account_name>(30, "alice1111111"_n) ) );

   auto prod = get_producer_info( "alice1111111" );
   BOOST_TEST_REQUIRE( 0 == prod["total_votes"].as_int64() );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( producer_keep_votes, eosio_system_tester, * boost::unit_test::tolerance(1e+5) ) try {
   issue_and_transfer( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   fc::variant params = producer_parameters_example(1);
   vector<char> key = fc::raw::pack( get_public_key( "alice1111111"_n, "active" ) );
   BOOST_REQUIRE_EQUAL( success(), push_action( "alice1111111"_n, "regproducer"_n, mvo()
                                               ("producer",  "alice1111111")
                                               ("producer_key", get_public_key( "alice1111111"_n, "active") )
                                               ("url", "")
                                               ("location", 0)
                        )
   );

   //bob111111111 makes stake
   issue_and_transfer( "bob111111111", core_sym::from_string("2000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), addvote( "bob111111111", core_sym::from_string("13.0000"), core_sym::from_string("0.5791") ) );
   REQUIRE_MATCHING_OBJECT( voter( "bob111111111", core_sym::from_string("13.5791") ), get_voter_info( "bob111111111" ) );

   //bob111111111 votes for alice1111111
   BOOST_REQUIRE_EQUAL( success(), vote("bob111111111"_n, { "alice1111111"_n } ) );

   auto prod = get_producer_info( "alice1111111" );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("13.5791")) == prod["total_votes"].as_double() );

   //unregister producer
   BOOST_REQUIRE_EQUAL( success(), push_action("alice1111111"_n, "unregprod"_n, mvo()
                                               ("producer",  "alice1111111")
                        )
   );
   prod = get_producer_info( "alice1111111" );
   //key should be empty
   BOOST_REQUIRE_EQUAL( fc::crypto::public_key(), fc::crypto::public_key(prod["producer_key"].as_string()) );
   //check parameters just in case
   //REQUIRE_MATCHING_OBJECT( params, prod["prefs"]);
   //votes should stay the same
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("13.5791")), prod["total_votes"].as_double() );

   //regtister the same producer again
   params = producer_parameters_example(2);
   BOOST_REQUIRE_EQUAL( success(), push_action( "alice1111111"_n, "regproducer"_n, mvo()
                                               ("producer",  "alice1111111")
                                               ("producer_key", get_public_key( "alice1111111"_n, "active") )
                                               ("url", "")
                                               ("location", 0)
                        )
   );
   prod = get_producer_info( "alice1111111" );
   //votes should stay the same
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("13.5791")), prod["total_votes"].as_double() );

   //change parameters
   params = producer_parameters_example(3);
   BOOST_REQUIRE_EQUAL( success(), push_action( "alice1111111"_n, "regproducer"_n, mvo()
                                               ("producer",  "alice1111111")
                                               ("producer_key", get_public_key( "alice1111111"_n, "active") )
                                               ("url","")
                                               ("location", 0)
                        )
   );
   prod = get_producer_info( "alice1111111" );
   //votes should stay the same
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("13.5791")), prod["total_votes"].as_double() );
   //check parameters just in case
   //REQUIRE_MATCHING_OBJECT( params, prod["prefs"]);

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( vote_for_two_producers, eosio_system_tester, * boost::unit_test::tolerance(1e+5) ) try {
   //alice1111111 becomes a producer
   fc::variant params = producer_parameters_example(1);
   auto key = get_public_key( "alice1111111"_n, "active" );
   BOOST_REQUIRE_EQUAL( success(), push_action( "alice1111111"_n, "regproducer"_n, mvo()
                                               ("producer",  "alice1111111")
                                               ("producer_key", get_public_key( "alice1111111"_n, "active") )
                                               ("url","")
                                               ("location", 0)
                        )
   );
   //bob111111111 becomes a producer
   params = producer_parameters_example(2);
   key = get_public_key( "bob111111111"_n, "active" );
   BOOST_REQUIRE_EQUAL( success(), push_action( "bob111111111"_n, "regproducer"_n, mvo()
                                               ("producer",  "bob111111111")
                                               ("producer_key", get_public_key( "alice1111111"_n, "active") )
                                               ("url","")
                                               ("location", 0)
                        )
   );

   //carol1111111 votes for alice1111111 and bob111111111
   issue_and_transfer( "carol1111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), addvote( "carol1111111", core_sym::from_string("15.0005"), core_sym::from_string("5.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), vote( "carol1111111"_n, { "alice1111111"_n, "bob111111111"_n } ) );

   auto alice_info = get_producer_info( "alice1111111" );
   // One account's aggregate voting power is divided equally across its selections.
   // The indivisible 0.0001 remainder is intentionally left unallocated.
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("10.0002")) == alice_info["total_votes"].as_int64() );
   auto bob_info = get_producer_info( "bob111111111" );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("10.0002")) == bob_info["total_votes"].as_int64() );

   //carol1111111 votes for alice1111111 (but revokes vote for bob111111111)
   BOOST_REQUIRE_EQUAL( success(), vote( "carol1111111"_n, { "alice1111111"_n } ) );

   alice_info = get_producer_info( "alice1111111" );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("20.0005")) == alice_info["total_votes"].as_int64() );
   bob_info = get_producer_info( "bob111111111" );
   BOOST_TEST_REQUIRE( 0 == bob_info["total_votes"].as_int64() );

   //alice1111111 votes for herself and bob111111111
   issue_and_transfer( "alice1111111", core_sym::from_string("2.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), addvote( "alice1111111", core_sym::from_string("1.0000"), core_sym::from_string("1.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), vote("alice1111111"_n, { "alice1111111"_n, "bob111111111"_n } ) );

   alice_info = get_producer_info( "alice1111111" );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("21.0005")) == alice_info["total_votes"].as_int64() );

   bob_info = get_producer_info( "bob111111111" );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("1.0000")) == bob_info["total_votes"].as_int64() );

   // Adding stake while keeping two selections must recompute both retained allocations.
   issue_and_transfer( "alice1111111", core_sym::from_string("1.0001"), config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), addvote( "alice1111111"_n, core_sym::from_string("1.0001") ) );

   alice_info = get_producer_info( "alice1111111" );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("21.5005")) == alice_info["total_votes"].as_int64() );
   bob_info = get_producer_info( "bob111111111" );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("1.5000")) == bob_info["total_votes"].as_int64() );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(eosio_system_votepay_tests)

BOOST_FIXTURE_TEST_CASE(producers_upgrade_system_contract, eosio_system_tester) try {
   //install multisig contract
   abi_serializer msig_abi_ser = initialize_multisig();
   auto producer_names = active_and_vote_producers();

   //change `default_max_inline_action_size` to 512 KB
   eosio::chain::chain_config params = control->get_global_properties().configuration;
   params.max_inline_action_size = 512 * 1024;
   base_tester::push_action( config::system_account_name, "setparams"_n, config::system_account_name, mutable_variant_object()
                              ("params", params) );

   produce_blocks();

   //helper function
   auto push_action_msig = [&]( const account_name& signer, const action_name &name, const variant_object &data, bool auth = true ) -> action_result {
         string action_type_name = msig_abi_ser.get_action_type(name);

         action act;
         act.account = "flon.msig"_n;
         act.name = name;
         act.data = msig_abi_ser.variant_to_binary( action_type_name, data, abi_serializer::create_yield_function(abi_serializer_max_time) );

         return base_tester::push_action( std::move(act), (auth ? signer : signer == "bob111111111"_n ? "alice1111111"_n : "bob111111111"_n).to_uint64_t() );
   };
   // test begins
   vector<permission_level> prod_perms;
   for ( auto& x : producer_names ) {
      prod_perms.push_back( { name(x), config::active_name } );
   }

   transaction trx;
   {
      //prepare system contract with different hash (contract differs in one byte)
      auto code = contracts::system_wasm();
      string msg = "producer votes must be unique and sorted";
      auto it = std::search( code.begin(), code.end(), msg.begin(), msg.end() );
      BOOST_REQUIRE( it != code.end() );
      msg[0] = 'P';
      std::copy( msg.begin(), msg.end(), it );

      fc::variant pretty_trx = fc::mutable_variant_object()
         ("expiration", "2020-01-01T00:30")
         ("ref_block_num", 2)
         ("ref_block_prefix", 3)
         ("net_usage_words", 0)
         ("max_cpu_usage_ms", 0)
         ("delay_sec", 0)
         ("actions", fc::variants({
               fc::mutable_variant_object()
                  ("account", name(config::system_account_name))
                  ("name", "setcode")
                  ("authorization", vector<permission_level>{ { config::system_account_name, config::active_name } })
                  ("data", fc::mutable_variant_object() ("account", name(config::system_account_name))
                   ("vmtype", 0)
                   ("vmversion", "0")
                   ("code", bytes( code.begin(), code.end() ))
                  )
                  })
         );
      abi_serializer::from_variant(pretty_trx, trx, get_resolver(), abi_serializer::create_yield_function(abi_serializer_max_time));
   }

   BOOST_REQUIRE_EQUAL(success(), push_action_msig( "alice1111111"_n, "propose"_n, mvo()
                                                    ("proposer",      "alice1111111")
                                                    ("proposal_name", "upgrade1")
                                                    ("trx",           trx)
                                                    ("requested", prod_perms)
                       )
   );

   // get 15 approvals
   for ( size_t i = 0; i < 14; ++i ) {
      BOOST_REQUIRE_EQUAL(success(), push_action_msig( name(producer_names[i]), "approve"_n, mvo()
                                                       ("proposer",      "alice1111111")
                                                       ("proposal_name", "upgrade1")
                                                       ("level",         permission_level{ name(producer_names[i]), config::active_name })
                          )
      );
   }

   //should fail
   BOOST_REQUIRE_EQUAL(wasm_assert_msg("transaction authorization failed"),
                       push_action_msig( "alice1111111"_n, "exec"_n, mvo()
                                         ("proposer",      "alice1111111")
                                         ("proposal_name", "upgrade1")
                                         ("executer",      "alice1111111")
                       )
   );

   // one more approval
   BOOST_REQUIRE_EQUAL(success(), push_action_msig( name(producer_names[14]), "approve"_n, mvo()
                                                    ("proposer",      "alice1111111")
                                                    ("proposal_name", "upgrade1")
                                                    ("level",         permission_level{ name(producer_names[14]), config::active_name })
                          )
   );

   transaction_trace_ptr trace;
   control->applied_transaction().connect(
   [&]( std::tuple<const transaction_trace_ptr&, const packed_transaction_ptr&> p ) {
      trace = std::get<0>(p);
   } );

   BOOST_REQUIRE_EQUAL(success(), push_action_msig( "alice1111111"_n, "exec"_n, mvo()
                                                    ("proposer",      "alice1111111")
                                                    ("proposal_name", "upgrade1")
                                                    ("executer",      "alice1111111")
                       )
   );

   BOOST_REQUIRE( bool(trace) );
   BOOST_REQUIRE_EQUAL( 1u, trace->action_traces.size() );
   BOOST_REQUIRE_EQUAL( transaction_receipt::executed, trace->receipt->status );
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(producer_onblock_check, eosio_system_tester) try {
   const name producer = "producer1111"_n;
   const name voter_name = "alice1111111"_n;
   setup_producer_accounts({producer});
   BOOST_REQUIRE_EQUAL(success(), regproducer(producer));
   issue_and_transfer(voter_name, core_sym::from_string("100.0000"), config::system_account_name);
   BOOST_REQUIRE_EQUAL(success(), addvote(voter_name, core_sym::from_string("100.0000")));
   BOOST_REQUIRE_EQUAL(success(), vote(voter_name, {producer}));
   BOOST_REQUIRE_EQUAL(wasm_assert_msg("cannot subvote until the election is activated"),
                       subvote(voter_name, core_sym::from_string("1.0000")));
   BOOST_REQUIRE_EQUAL(success(), setprodcfg());
   produce_block(fc::hours(2));
   produce_blocks(100);
   BOOST_REQUIRE_EQUAL(core_sym::from_string("100.0000").get_amount(),
                       get_producer_info(producer)["total_votes"].as_int64());
} FC_LOG_AND_RETHROW()

fc::mutable_variant_object config_to_variant( const eosio::chain::chain_config& config ) {
   return mutable_variant_object()
      ( "max_block_net_usage", config.max_block_net_usage )
      ( "target_block_net_usage_pct", config.target_block_net_usage_pct )
      ( "max_transaction_net_usage", config.max_transaction_net_usage )
      ( "base_per_transaction_net_usage", config.base_per_transaction_net_usage )
      ( "context_free_discount_net_usage_num", config.context_free_discount_net_usage_num )
      ( "context_free_discount_net_usage_den", config.context_free_discount_net_usage_den )
      ( "max_block_cpu_usage", config.max_block_cpu_usage )
      ( "target_block_cpu_usage_pct", config.target_block_cpu_usage_pct )
      ( "max_transaction_cpu_usage", config.max_transaction_cpu_usage )
      ( "min_transaction_cpu_usage", config.min_transaction_cpu_usage )
      ( "max_transaction_lifetime", config.max_transaction_lifetime )
      ( "deferred_trx_expiration_window", config.deferred_trx_expiration_window )
      ( "max_transaction_delay", config.max_transaction_delay )
      ( "max_inline_action_size", config.max_inline_action_size )
      ( "max_inline_action_depth", config.max_inline_action_depth )
      ( "max_authority_depth", config.max_authority_depth );
}

BOOST_FIXTURE_TEST_CASE(elect_producers, eosio_system_tester) try {
   const name first = "defproducer1"_n;
   const name second = "defproducer2"_n;
   const name voter_name = "alice1111111"_n;
   setup_producer_accounts({first, second});
   BOOST_REQUIRE_EQUAL(success(), regproducer(first));
   BOOST_REQUIRE_EQUAL(success(), regproducer(second));
   issue_and_transfer(voter_name, core_sym::from_string("100.0000"), config::system_account_name);
   BOOST_REQUIRE_EQUAL(success(), setprodcfg());
   BOOST_REQUIRE_EQUAL(success(), addvote(voter_name, core_sym::from_string("100.0000")));
   BOOST_REQUIRE_EQUAL(success(), vote(voter_name, {first, second}));
   produce_block(fc::hours(2));
   produce_blocks(100);
   BOOST_REQUIRE_EQUAL(core_sym::from_string("50.0000").get_amount(),
                       get_producer_info(first)["total_votes"].as_int64());
   BOOST_REQUIRE_EQUAL(core_sym::from_string("50.0000").get_amount(),
                       get_producer_info(second)["total_votes"].as_int64());
   // Savanna selection additionally requires registered finalizer keys; the
   // schedule transition itself is covered by finalizer policy tests.
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
BOOST_AUTO_TEST_SUITE(eosio_system_name_tests)

// Name auctions are disabled in the current system contract. Cover the
// active account naming rules instead of the retired bidname action.
BOOST_FIXTURE_TEST_CASE(short_name_requires_system_creator, eosio_system_tester) try {
   create_accounts_with_resources({"dan"_n});
   BOOST_REQUIRE_EXCEPTION(create_accounts_with_resources({"shortname"_n}, "dan"_n),
      eosio_assert_message_exception, eosio_assert_message_is("name bid not supported"));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(system_can_create_short_name, eosio_system_tester) try {
   BOOST_REQUIRE_NO_THROW(create_accounts_with_resources({"shortname"_n}));
   BOOST_REQUIRE((control->db().find<account_object, by_name>("shortname"_n) != nullptr));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(suffix_owner_can_create_subaccount, eosio_system_tester) try {
   create_accounts_with_resources({"dan"_n});
   BOOST_REQUIRE_NO_THROW(create_accounts_with_resources({"child.dan"_n}, "dan"_n));
   BOOST_REQUIRE((control->db().find<account_object, by_name>("child.dan"_n) != nullptr));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(unrelated_creator_cannot_use_suffix, eosio_system_tester) try {
   create_accounts_with_resources({"dan"_n, "sam"_n});
   BOOST_REQUIRE_EXCEPTION(create_accounts_with_resources({"child.dan"_n}, "sam"_n),
      eosio_assert_message_exception, eosio_assert_message_is("only suffix may create this account"));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(twelve_character_name_needs_no_bid, eosio_system_tester) try {
   create_accounts_with_resources({"dan"_n});
   BOOST_REQUIRE_NO_THROW(create_accounts_with_resources({"abcdefgh1234"_n}, "dan"_n));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( vote_producers_in_and_out, eosio_system_tester ) try {

   const asset net = core_sym::from_string("80.0000");
   const asset cpu = core_sym::from_string("80.0000");
   std::vector<account_name> voters = { "producvotera"_n, "producvoterb"_n, "producvoterc"_n, "producvoterd"_n };
   for (const auto& v: voters) {
      create_account_with_resources(v, config::system_account_name, core_sym::from_string("1.0000"), false, net, cpu);
   }

   // create accounts {defproducera, defproducerb, ..., defproducerz} and register as producers
   std::vector<account_name> producer_names;
   {
      producer_names.reserve('z' - 'a' + 1);
      const std::string root("defproducer");
      for ( char c = 'a'; c <= 'z'; ++c ) {
         producer_names.emplace_back(root + std::string(1, c));
      }
      setup_producer_accounts(producer_names);
      for (const auto& p: producer_names) {
         BOOST_REQUIRE_EQUAL( success(), regproducer(p) );
         produce_blocks(1);
         ilog( "------ get pro----------" );
         wdump((p));
         BOOST_TEST(0 == get_producer_info(p)["total_votes"].as<double>());
      }
   }

   for (const auto& v: voters) {
      transfer( config::system_account_name, v, core_sym::from_string("200000000.0000"), config::system_account_name );
      BOOST_REQUIRE_EQUAL(success(), addvote(v, core_sym::from_string("30000000.0000"), core_sym::from_string("30000000.0000")) );
   }

   {
      BOOST_REQUIRE_EQUAL(success(), vote("producvotera"_n, vector<account_name>(producer_names.begin(), producer_names.begin()+20)));
      BOOST_REQUIRE_EQUAL(success(), vote("producvoterb"_n, vector<account_name>(producer_names.begin(), producer_names.begin()+21)));
      BOOST_REQUIRE_EQUAL(success(), vote("producvoterc"_n, vector<account_name>(producer_names.begin(), producer_names.end())));
   }

   // TODO: ENABLE_PRODUCER_REWARD
   #ifdef ENABLE_PRODUCER_REWARD
   // give a chance for everyone to produce blocks
   {
      produce_blocks(23 * 12 + 20);
      bool all_21_produced = true;
      for (uint32_t i = 0; i < 21; ++i) {
         if (0 == get_producer_info(producer_names[i])["unpaid_blocks"].as<uint32_t>()) {
            all_21_produced = false;
         }
      }
      bool rest_didnt_produce = true;
      for (uint32_t i = 21; i < producer_names.size(); ++i) {
         if (0 < get_producer_info(producer_names[i])["unpaid_blocks"].as<uint32_t>()) {
            rest_didnt_produce = false;
         }
      }
      BOOST_REQUIRE(all_21_produced && rest_didnt_produce);
   }

   {
      produce_block(fc::hours(7));
      const uint32_t voted_out_index = 20;
      const uint32_t new_prod_index  = 23;
      BOOST_REQUIRE_EQUAL(success(), addvote("producvoterd", core_sym::from_string("40000000.0000"), core_sym::from_string("40000000.0000")));
      BOOST_REQUIRE_EQUAL(success(), vote("producvoterd"_n, { producer_names[new_prod_index] }));
      BOOST_REQUIRE_EQUAL(0u, get_producer_info(producer_names[new_prod_index])["unpaid_blocks"].as<uint32_t>());
      produce_blocks(4 * 12 * 21);
      BOOST_REQUIRE(0 < get_producer_info(producer_names[new_prod_index])["unpaid_blocks"].as<uint32_t>());
      const uint32_t initial_unpaid_blocks = get_producer_info(producer_names[voted_out_index])["unpaid_blocks"].as<uint32_t>();
      produce_blocks(2 * 12 * 21);
      BOOST_REQUIRE_EQUAL(initial_unpaid_blocks, get_producer_info(producer_names[voted_out_index])["unpaid_blocks"].as<uint32_t>());
      produce_block(fc::hours(24));
      BOOST_REQUIRE_EQUAL(success(), vote("producvoterd"_n, { producer_names[voted_out_index] }));
      produce_blocks(2 * 12 * 21);
      BOOST_REQUIRE(fc::crypto::public_key() != fc::crypto::public_key(get_producer_info(producer_names[voted_out_index])["producer_key"].as_string()));
      BOOST_REQUIRE_EQUAL(success(), push_action(producer_names[voted_out_index], "claimrewards"_n,
                                                mvo()("caller", producer_names[voted_out_index])
                                                     ("producer", producer_names[voted_out_index])
                                                     ("max_steps", 24))));
   }
   #endif//ENABLE_PRODUCER_REWARD

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( setparams, eosio_system_tester ) try {
   //install multisig contract
   abi_serializer msig_abi_ser = initialize_multisig();
   auto producer_names = active_and_vote_producers();

   //helper function
   auto push_action_msig = [&]( const account_name& signer, const action_name &name, const variant_object &data, bool auth = true ) -> action_result {
         string action_type_name = msig_abi_ser.get_action_type(name);

         action act;
         act.account = "flon.msig"_n;
         act.name = name;
         act.data = msig_abi_ser.variant_to_binary( action_type_name, data, abi_serializer::create_yield_function(abi_serializer_max_time) );

         return base_tester::push_action( std::move(act), (auth ? signer : signer == "bob111111111"_n ? "alice1111111"_n : "bob111111111"_n).to_uint64_t() );
   };

   // test begins
   vector<permission_level> prod_perms;
   for ( auto& x : producer_names ) {
      prod_perms.push_back( { name(x), config::active_name } );
   }

   eosio::chain::chain_config params;
   params = control->get_global_properties().configuration;
   //change some values
   params.max_block_net_usage += 10;
   params.max_transaction_lifetime += 1;

   transaction trx;
   {
      fc::variant pretty_trx = fc::mutable_variant_object()
         ("expiration", "2020-01-01T00:30")
         ("ref_block_num", 2)
         ("ref_block_prefix", 3)
         ("net_usage_words", 0)
         ("max_cpu_usage_ms", 0)
         ("delay_sec", 0)
         ("actions", fc::variants({
               fc::mutable_variant_object()
                  ("account", name(config::system_account_name))
                  ("name", "setparams")
                  ("authorization", vector<permission_level>{ { config::system_account_name, config::active_name } })
                  ("data", fc::mutable_variant_object()
                   ("params", params)
                  )
                  })
         );
      abi_serializer::from_variant(pretty_trx, trx, get_resolver(), abi_serializer::create_yield_function(abi_serializer_max_time));
   }

   BOOST_REQUIRE_EQUAL(success(), push_action_msig( "alice1111111"_n, "propose"_n, mvo()
                                                    ("proposer",      "alice1111111")
                                                    ("proposal_name", "setparams1")
                                                    ("trx",           trx)
                                                    ("requested", prod_perms)
                       )
   );

   // get 16 approvals
   for ( size_t i = 0; i < 15; ++i ) {
      BOOST_REQUIRE_EQUAL(success(), push_action_msig( name(producer_names[i]), "approve"_n, mvo()
                                                       ("proposer",      "alice1111111")
                                                       ("proposal_name", "setparams1")
                                                       ("level",         permission_level{ name(producer_names[i]), config::active_name })
                          )
      );
   }

   transaction_trace_ptr trace;
   control->applied_transaction().connect(
   [&]( std::tuple<const transaction_trace_ptr&, const packed_transaction_ptr&> p ) {
      trace = std::get<0>(p);
   } );

   BOOST_REQUIRE_EQUAL(success(), push_action_msig( "alice1111111"_n, "exec"_n, mvo()
                                                    ("proposer",      "alice1111111")
                                                    ("proposal_name", "setparams1")
                                                    ("executer",      "alice1111111")
                       )
   );

   BOOST_REQUIRE( bool(trace) );
   BOOST_REQUIRE_EQUAL( 1u, trace->action_traces.size() );
   BOOST_REQUIRE_EQUAL( transaction_receipt::executed, trace->receipt->status );

   produce_blocks( 250 );

   // make sure that changed parameters were applied
   auto active_params = control->get_global_properties().configuration;
   BOOST_REQUIRE_EQUAL( params.max_block_net_usage, active_params.max_block_net_usage );
   BOOST_REQUIRE_EQUAL( params.max_transaction_lifetime, active_params.max_transaction_lifetime );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( wasmcfg, eosio_system_tester ) try {
   //install multisig contract
   abi_serializer msig_abi_ser = initialize_multisig();
   auto producer_names = active_and_vote_producers();

   //helper function
   auto push_action_msig = [&]( const account_name& signer, const action_name &name, const variant_object &data, bool auth = true ) -> action_result {
         string action_type_name = msig_abi_ser.get_action_type(name);

         action act;
         act.account = "flon.msig"_n;
         act.name = name;
         act.data = msig_abi_ser.variant_to_binary( action_type_name, data, abi_serializer::create_yield_function(abi_serializer_max_time) );

         return base_tester::push_action( std::move(act), (auth ? signer : signer == "bob111111111"_n ? "alice1111111"_n : "bob111111111"_n).to_uint64_t() );
   };

   // test begins
   vector<permission_level> prod_perms;
   for ( auto& x : producer_names ) {
      prod_perms.push_back( { name(x), config::active_name } );
   }

   transaction trx;
   {
      fc::variant pretty_trx = fc::mutable_variant_object()
         ("expiration", "2020-01-01T00:30")
         ("ref_block_num", 2)
         ("ref_block_prefix", 3)
         ("net_usage_words", 0)
         ("max_cpu_usage_ms", 0)
         ("delay_sec", 0)
         ("actions", fc::variants({
               fc::mutable_variant_object()
                  ("account", name(config::system_account_name))
                  ("name", "wasmcfg")
                  ("authorization", vector<permission_level>{ { config::system_account_name, config::active_name } })
                  ("data", fc::mutable_variant_object()
                   ("settings", "high"_n)
                  )
                  })
         );
      abi_serializer::from_variant(pretty_trx, trx, get_resolver(), abi_serializer::create_yield_function(abi_serializer_max_time));
   }

   BOOST_REQUIRE_EQUAL(success(), push_action_msig( "alice1111111"_n, "propose"_n, mvo()
                                                    ("proposer",      "alice1111111")
                                                    ("proposal_name", "setparams1")
                                                    ("trx",           trx)
                                                    ("requested", prod_perms)
                       )
   );

   // get 16 approvals
   for ( size_t i = 0; i < 15; ++i ) {
      BOOST_REQUIRE_EQUAL(success(), push_action_msig( name(producer_names[i]), "approve"_n, mvo()
                                                       ("proposer",      "alice1111111")
                                                       ("proposal_name", "setparams1")
                                                       ("level",         permission_level{ name(producer_names[i]), config::active_name })
                          )
      );
   }

   transaction_trace_ptr trace;
   control->applied_transaction().connect(
   [&]( std::tuple<const transaction_trace_ptr&, const packed_transaction_ptr&> p ) {
      trace = std::get<0>(p);
   } );

   BOOST_REQUIRE_EQUAL(success(), push_action_msig( "alice1111111"_n, "exec"_n, mvo()
                                                    ("proposer",      "alice1111111")
                                                    ("proposal_name", "setparams1")
                                                    ("executer",      "alice1111111")
                       )
   );

   BOOST_REQUIRE( bool(trace) );
   BOOST_REQUIRE_EQUAL( 1u, trace->action_traces.size() );
   BOOST_REQUIRE_EQUAL( transaction_receipt::executed, trace->receipt->status );

   produce_blocks( 250 );

   // make sure that changed parameters were applied
   auto active_params = control->get_global_properties().wasm_configuration;
   BOOST_REQUIRE_EQUAL( active_params.max_table_elements, 8192u );
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
BOOST_AUTO_TEST_SUITE(eosio_system_part5_tests)

BOOST_FIXTURE_TEST_CASE(gas_purchase_increases_limit, eosio_system_tester) try {
   const name account = "alice1111111"_n;
   transfer("flon"_n, account, core_sym::from_string("200.0000"));
   uint64_t before = 0;
   bool unlimited = false;
   control->get_resource_limits_manager().get_account_limits(account, before, unlimited);
   BOOST_REQUIRE_EQUAL(success(), push_action(account, "buygasself"_n,
      mvo()("account", account)("quant", "100.0000 TST")));
   uint64_t after = 0;
   control->get_resource_limits_manager().get_account_limits(account, after, unlimited);
   BOOST_REQUIRE_GT(after, before);
   BOOST_REQUIRE(!unlimited);
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(gas_purchase_funds_system_pool, eosio_system_tester) try {
   const name account = "alice1111111"_n;
   transfer("flon"_n, account, core_sym::from_string("10.0000"));
   const auto before = get_balance("flon.gas"_n);
   BOOST_REQUIRE_EQUAL(success(), push_action(account, "buygasself"_n,
      mvo()("account", account)("quant", "1.0000 TST")));
   BOOST_REQUIRE_EQUAL(before + core_sym::from_string("1.0000"), get_balance("flon.gas"_n));
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(eosio_system_part6_tests)

BOOST_AUTO_TEST_CASE( setabi_bios ) try {
   fc::temp_directory tempdir;
   validating_tester t( tempdir, true );
   t.execute_setup_policy( setup_policy::full );

   abi_serializer abi_ser(fc::json::from_string( (const char*)contracts::bios_abi().data()).template as<abi_def>(), abi_serializer::create_yield_function(base_tester::abi_serializer_max_time));
   t.set_code( config::system_account_name, contracts::bios_wasm() );
   t.set_abi( config::system_account_name, contracts::bios_abi().data() );
   t.create_account("flon.token"_n);
   t.set_abi( "flon.token"_n, contracts::token_abi().data() );
   {
      auto res = t.get_row_by_account( config::system_account_name, config::system_account_name, "abihash"_n, "flon.token"_n );
      _abi_hash abi_hash;
      auto abi_hash_var = abi_ser.binary_to_variant( "abi_hash", res, abi_serializer::create_yield_function(base_tester::abi_serializer_max_time) );
      abi_serializer::from_variant( abi_hash_var, abi_hash, t.get_resolver(), abi_serializer::create_yield_function(base_tester::abi_serializer_max_time));
      auto abi = fc::raw::pack(fc::json::from_string( (const char*)contracts::token_abi().data()).template as<abi_def>());
      auto result = fc::sha256::hash( (const char*)abi.data(), abi.size() );

      BOOST_REQUIRE( abi_hash.hash == result );
   }

   t.set_abi( "flon.token"_n, contracts::system_abi().data() );
   {
      auto res = t.get_row_by_account( config::system_account_name, config::system_account_name, "abihash"_n, "flon.token"_n );
      _abi_hash abi_hash;
      auto abi_hash_var = abi_ser.binary_to_variant( "abi_hash", res, abi_serializer::create_yield_function(base_tester::abi_serializer_max_time) );
      abi_serializer::from_variant( abi_hash_var, abi_hash, t.get_resolver(), abi_serializer::create_yield_function(base_tester::abi_serializer_max_time));
      auto abi = fc::raw::pack(fc::json::from_string( (const char*)contracts::system_abi().data()).template as<abi_def>());
      auto result = fc::sha256::hash( (const char*)abi.data(), abi.size() );

      BOOST_REQUIRE( abi_hash.hash == result );
   }
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( setabi, eosio_system_tester ) try {
   set_abi( "flon.token"_n, contracts::token_abi().data() );
   {
      auto res = get_row_by_account( config::system_account_name, config::system_account_name, "abihash"_n, "flon.token"_n );
      _abi_hash abi_hash;
      auto abi_hash_var = abi_ser.binary_to_variant( "abi_hash", res, abi_serializer::create_yield_function(abi_serializer_max_time) );
      abi_serializer::from_variant( abi_hash_var, abi_hash, get_resolver(), abi_serializer::create_yield_function(abi_serializer_max_time));
      auto abi = fc::raw::pack(fc::json::from_string( (const char*)contracts::token_abi().data()).template as<abi_def>());
      auto result = fc::sha256::hash( (const char*)abi.data(), abi.size() );

      BOOST_REQUIRE( abi_hash.hash == result );
   }

   set_abi( "flon.token"_n, contracts::system_abi().data() );
   {
      auto res = get_row_by_account( config::system_account_name, config::system_account_name, "abihash"_n, "flon.token"_n );
      _abi_hash abi_hash;
      auto abi_hash_var = abi_ser.binary_to_variant( "abi_hash", res, abi_serializer::create_yield_function(abi_serializer_max_time) );
      abi_serializer::from_variant( abi_hash_var, abi_hash, get_resolver(), abi_serializer::create_yield_function(abi_serializer_max_time));
      auto abi = fc::raw::pack(fc::json::from_string( (const char*)contracts::system_abi().data()).template as<abi_def>());
      auto result = fc::sha256::hash( (const char*)abi.data(), abi.size() );

      BOOST_REQUIRE( abi_hash.hash == result );
   }

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(change_limited_account_back_to_unlimited, eosio_system_tester) try {
   const name account = "alice1111111"_n;
   uint64_t gas = 0;
   bool unlimited = true;
   control->get_resource_limits_manager().get_account_limits(account, gas, unlimited);
   BOOST_REQUIRE(!unlimited);
   BOOST_REQUIRE_EQUAL(success(), push_action("flon"_n, "setalimits"_n,
      mvo()("account", account)("gas", gas)("is_unlimited", true)));
   control->get_resource_limits_manager().get_account_limits(account, gas, unlimited);
   BOOST_REQUIRE(unlimited);
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(insufficient_balance_rejects_gas_purchase, eosio_system_tester) try {
   uint64_t before = 0;
   bool unlimited = false;
   control->get_resource_limits_manager().get_account_limits("alice1111111"_n, before, unlimited);
   BOOST_REQUIRE_EQUAL(core_sym::from_string("0.0000"), get_balance("alice1111111"_n));
   BOOST_REQUIRE_NE(success(), push_action("alice1111111"_n, "buygasself"_n,
      mvo()("account", "alice1111111")("quant", "1.0000 TST")));
   uint64_t after = 0;
   control->get_resource_limits_manager().get_account_limits("alice1111111"_n, after, unlimited);
   BOOST_REQUIRE_EQUAL(before, after);
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()

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

BOOST_AUTO_TEST_SUITE(eosio_system_stake_tests)

BOOST_FIXTURE_TEST_CASE( buysell, eosio_system_tester ) try {

   BOOST_REQUIRE_EQUAL( core_sym::from_string("0.0000"), get_balance( "alice1111111" ) );

   transfer( "flon", "alice1111111", core_sym::from_string("1000.0000"), "flon" );
   BOOST_REQUIRE_EQUAL( success(), stake( "flon", "alice1111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );

   auto total = get_total_stake( "alice1111111" );
   auto init_bytes =  total["ram_bytes"].as_uint64();

   const asset initial_ram_balance = get_balance("flon.ram"_n);
   const asset initial_fees_balance = get_balance("flon.fees"_n);
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("200.0000") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("800.0000"), get_balance( "alice1111111" ) );
   BOOST_REQUIRE_EQUAL( initial_ram_balance + core_sym::from_string("199.0000"), get_balance("flon.ram"_n) );
   BOOST_REQUIRE_EQUAL( initial_fees_balance + core_sym::from_string("1.0000"), get_balance("flon.fees"_n) );

   total = get_total_stake( "alice1111111" );
   auto bytes = total["ram_bytes"].as_uint64();
   auto bought_bytes = bytes - init_bytes;
   wdump((init_bytes)(bought_bytes)(bytes) );

   BOOST_REQUIRE_EQUAL( true, 0 < bought_bytes );

   BOOST_REQUIRE_EQUAL( success(), sellram( "alice1111111", bought_bytes ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("998.0049"), get_balance( "alice1111111" ) );
   total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( true, total["ram_bytes"].as_uint64() == init_bytes );

   transfer( "flon", "alice1111111", core_sym::from_string("100000000.0000"), "flon" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("100000998.0049"), get_balance( "alice1111111" ) );
   // alice buys ram for 10000000.0000, 0.5% = 50000.0000 go to ramfee
   // after fee 9950000.0000 go to bought bytes
   // when selling back bought bytes, pay 0.5% fee and get back 99.5% of 9950000.0000 = 9900250.0000
   // expected account after that is 90000998.0049 + 9900250.0000 = 99901248.0049 with a difference
   // of order 0.0001 due to rounding errors
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("10000000.0000") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("90000998.0049"), get_balance( "alice1111111" ) );

   total = get_total_stake( "alice1111111" );
   bytes = total["ram_bytes"].as_uint64();
   bought_bytes = bytes - init_bytes;
   wdump((init_bytes)(bought_bytes)(bytes) );

   BOOST_REQUIRE_EQUAL( success(), sellram( "alice1111111", bought_bytes ) );
   total = get_total_stake( "alice1111111" );

   bytes = total["ram_bytes"].as_uint64();
   bought_bytes = bytes - init_bytes;
   wdump((init_bytes)(bought_bytes)(bytes) );

   BOOST_REQUIRE_EQUAL( true, total["ram_bytes"].as_uint64() == init_bytes );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("99901248.0048"), get_balance( "alice1111111" ) );

   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("100.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("100.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("100.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("100.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("100.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("10.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("10.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("10.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("30.0000") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("99900688.0048"), get_balance( "alice1111111" ) );

   auto newtotal = get_total_stake( "alice1111111" );

   auto newbytes = newtotal["ram_bytes"].as_uint64();
   bought_bytes = newbytes - bytes;
   wdump((newbytes)(bytes)(bought_bytes) );

   BOOST_REQUIRE_EQUAL( success(), sellram( "alice1111111", bought_bytes ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("99901242.4187"), get_balance( "alice1111111" ) );

   newtotal = get_total_stake( "alice1111111" );
   auto startbytes = newtotal["ram_bytes"].as_uint64();

   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("10000000.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("10000000.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("10000000.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("10000000.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("10000000.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("100000.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("100000.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("100000.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("300000.0000") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("49301242.4187"), get_balance( "alice1111111" ) );

   auto finaltotal = get_total_stake( "alice1111111" );
   auto endbytes = finaltotal["ram_bytes"].as_uint64();

   bought_bytes = endbytes - startbytes;
   wdump((startbytes)(endbytes)(bought_bytes) );

   BOOST_REQUIRE_EQUAL( success(), sellram( "alice1111111", bought_bytes ) );

   BOOST_REQUIRE_EQUAL( false, get_row_by_account( config::system_account_name, config::system_account_name,
                                                   "rammarket"_n, account_name(symbol{SY(4,RAMCORE)}.value()) ).empty() );

   // auto get_ram_market = [this]() -> fc::variant {
   //    vector<char> data = get_row_by_account( config::system_account_name, config::system_account_name,
   //                                            "rammarket"_n, account_name(symbol{SY(4,RAMCORE)}.value()) );
   //    BOOST_REQUIRE( !data.empty() );
   //    return abi_ser.binary_to_variant("exchange_state", data, abi_serializer::create_yield_function(abi_serializer_max_time));
   // };

   // {
   //    transfer( config::system_account_name, "alice1111111"_n, core_sym::from_string("10000000.0000"), config::system_account_name );
   //    uint64_t bytes0 = get_total_stake( "alice1111111" )["ram_bytes"].as_uint64();

   //    auto market = get_ram_market();
   //    const asset r0 = market["base"].as<connector>().balance;
   //    const asset e0 = market["quote"].as<connector>().balance;
   //    BOOST_REQUIRE_EQUAL( asset::from_string("0 RAM").get_symbol(),     r0.get_symbol() );
   //    BOOST_REQUIRE_EQUAL( core_sym::from_string("0.0000").get_symbol(), e0.get_symbol() );

   //    const asset payment = core_sym::from_string("10000000.0000");
   //    BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", payment ) );
   //    uint64_t bytes1 = get_total_stake( "alice1111111" )["ram_bytes"].as_uint64();

   //    const int64_t fee = (payment.get_amount() + 199) / 200;
   //    const double net_payment = payment.get_amount() - fee;
   //    const uint64_t expected_delta = net_payment * r0.get_amount() / ( net_payment + e0.get_amount() );

   //    BOOST_REQUIRE_EQUAL( expected_delta, bytes1 -  bytes0 );
   // }

   // {
   //    transfer( config::system_account_name, "bob111111111"_n, core_sym::from_string("100000.0000"), config::system_account_name );
   //    BOOST_REQUIRE_EQUAL( wasm_assert_msg("must reserve a positive amount"),
   //                         buyrambytes( "bob111111111", "bob111111111", 1 ) );

   //    uint64_t bytes0 = get_total_stake( "bob111111111" )["ram_bytes"].as_uint64();
   //    BOOST_REQUIRE_EQUAL( success(), buyrambytes( "bob111111111", "bob111111111", 1024 ) );
   //    uint64_t bytes1 = get_total_stake( "bob111111111" )["ram_bytes"].as_uint64();
   //    BOOST_REQUIRE( within_one( 1024, bytes1 - bytes0 ) );

   //    BOOST_REQUIRE_EQUAL( success(), buyrambytes( "bob111111111", "bob111111111", 1024 * 1024) );
   //    uint64_t bytes2 = get_total_stake( "bob111111111" )["ram_bytes"].as_uint64();
   //    BOOST_REQUIRE( within_one( 1024 * 1024, bytes2 - bytes1 ) );
   // }

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( stake_unstake, eosio_system_tester ) try {
   cross_15_percent_threshold();

   produce_blocks( 10 );
   produce_block( fc::hours(3*24) );

   BOOST_REQUIRE_EQUAL( core_sym::from_string("0.0000"), get_balance( "alice1111111" ) );
   transfer( "flon", "alice1111111", core_sym::from_string("1000.0000"), "flon" );

   BOOST_REQUIRE_EQUAL( core_sym::from_string("1000.0000"), get_balance( "alice1111111" ) );
   BOOST_REQUIRE_EQUAL( success(), stake( "flon", "alice1111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );

   auto total = get_total_stake("alice1111111");
   BOOST_REQUIRE_EQUAL( core_sym::from_string("210.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("110.0000"), total["cpu_weight"].as<asset>());

   const auto init_eosio_stake_balance = get_balance( "flon.stake"_n );
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", "alice1111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );
   BOOST_REQUIRE_EQUAL( init_eosio_stake_balance + core_sym::from_string("300.0000"), get_balance( "flon.stake"_n ) );
   BOOST_REQUIRE_EQUAL( success(), unstake( "alice1111111", "alice1111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );

   produce_block( fc::hours(3*24-1) );
   produce_blocks(1);
   // testing balance still the same
   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );
   BOOST_REQUIRE_EQUAL( init_eosio_stake_balance + core_sym::from_string("300.0000"), get_balance( "flon.stake"_n ) );
   // call refund expected to fail too early
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("refund is not available yet"),
                       push_action( "alice1111111"_n, "refund"_n, mvo()("owner", "alice1111111") ) );

   // after 1 hour refund ready
   produce_block( fc::hours(1) );
   produce_blocks(1);
   // now we can do the refund
   BOOST_REQUIRE_EQUAL( success(), push_action( "alice1111111"_n, "refund"_n, mvo()("owner", "alice1111111") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("1000.0000"), get_balance( "alice1111111" ) );
   BOOST_REQUIRE_EQUAL( init_eosio_stake_balance, get_balance( "flon.stake"_n ) );

   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", "bob111111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );
   total = get_total_stake("bob111111111");
   BOOST_REQUIRE_EQUAL( core_sym::from_string("210.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("110.0000"), total["cpu_weight"].as<asset>());

   total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("210.0000").get_amount(), total["net_weight"].as<asset>().get_amount() );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("110.0000").get_amount(), total["cpu_weight"].as<asset>().get_amount() );

   // REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("300.0000")), get_voter_info( "alice1111111" ) );

   auto bytes = total["ram_bytes"].as_uint64();
   BOOST_REQUIRE_EQUAL( true, 0 < bytes );

   //unstake from bob111111111
   BOOST_REQUIRE_EQUAL( success(), unstake( "alice1111111", "bob111111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );
   total = get_total_stake("bob111111111");
   BOOST_REQUIRE_EQUAL( core_sym::from_string("10.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("10.0000"), total["cpu_weight"].as<asset>());
   produce_block( fc::hours(3*24-1) );
   produce_blocks(1);
   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );
   //after 3 days funds should be released
   produce_block( fc::hours(1) );
   produce_blocks(1);

   // REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("0.0000") ), get_voter_info( "alice1111111" ) );
   produce_blocks(1);
   BOOST_REQUIRE_EQUAL( success(), push_action( "alice1111111"_n, "refund"_n, mvo()("owner", "alice1111111") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("1000.0000"), get_balance( "alice1111111" ) );
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( stake_unstake_with_transfer, eosio_system_tester ) try {
   cross_15_percent_threshold();

   BOOST_REQUIRE_EQUAL( core_sym::from_string("0.0000"), get_balance( "alice1111111" ) );

   //eosio stakes for alice with transfer flag

   transfer( "flon", "bob111111111", core_sym::from_string("1000.0000"), "flon" );
   BOOST_REQUIRE_EQUAL( success(), stake_with_transfer( "bob111111111"_n, "alice1111111"_n, core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );

   //check that alice has both bandwidth and voting power
   auto total = get_total_stake("alice1111111");
   BOOST_REQUIRE_EQUAL( core_sym::from_string("210.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("110.0000"), total["cpu_weight"].as<asset>());
   // REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("300.0000")), get_voter_info( "alice1111111" ) );

   BOOST_REQUIRE_EQUAL( core_sym::from_string("0.0000"), get_balance( "alice1111111" ) );

   //alice stakes for herself
   transfer( "flon", "alice1111111", core_sym::from_string("1000.0000"), "flon" );
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", "alice1111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );
   //now alice's stake should be equal to transferred from eosio + own stake
   total = get_total_stake("alice1111111");
   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("410.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("210.0000"), total["cpu_weight"].as<asset>());
   // REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("600.0000")), get_voter_info( "alice1111111" ) );

   //alice can unstake everything (including what was transferred)
   BOOST_REQUIRE_EQUAL( success(), unstake( "alice1111111", "alice1111111", core_sym::from_string("400.0000"), core_sym::from_string("200.0000") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );

   produce_block( fc::hours(3*24-1) );
   produce_blocks(1);
   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );
   //after 3 days funds should be released

   produce_block( fc::hours(1) );
   produce_blocks(1);

   BOOST_REQUIRE_EQUAL( success(), push_action( "alice1111111"_n, "refund"_n, mvo()("owner", "alice1111111") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("1300.0000"), get_balance( "alice1111111" ) );

   //stake should be equal to what was staked in constructor, voting power should be 0
   total = get_total_stake("alice1111111");
   BOOST_REQUIRE_EQUAL( core_sym::from_string("10.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("10.0000"), total["cpu_weight"].as<asset>());
   // REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("0.0000")), get_voter_info( "alice1111111" ) );

   // Now alice stakes to bob with transfer flag
   BOOST_REQUIRE_EQUAL( success(), stake_with_transfer( "alice1111111"_n, "bob111111111"_n, core_sym::from_string("100.0000"), core_sym::from_string("100.0000") ) );

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( stake_to_self_with_transfer, eosio_system_tester ) try {
   cross_15_percent_threshold();

   BOOST_REQUIRE_EQUAL( core_sym::from_string("0.0000"), get_balance( "alice1111111" ) );
   transfer( "flon", "alice1111111", core_sym::from_string("1000.0000"), "flon" );

   BOOST_REQUIRE_EQUAL( wasm_assert_msg("cannot use transfer flag if delegating to self"),
                        stake_with_transfer( "alice1111111"_n, "alice1111111"_n, core_sym::from_string("200.0000"), core_sym::from_string("100.0000") )
   );

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( stake_while_pending_refund, eosio_system_tester ) try {
   cross_15_percent_threshold();

   BOOST_REQUIRE_EQUAL( core_sym::from_string("0.0000"), get_balance( "alice1111111" ) );

   //eosio stakes for alice with transfer flag
   transfer( "flon", "bob111111111", core_sym::from_string("1000.0000"), "flon" );
   BOOST_REQUIRE_EQUAL( success(), stake_with_transfer( "bob111111111"_n, "alice1111111"_n, core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );

   //check that alice has both bandwidth and voting power
   auto total = get_total_stake("alice1111111");
   BOOST_REQUIRE_EQUAL( core_sym::from_string("210.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("110.0000"), total["cpu_weight"].as<asset>());
   // REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("300.0000")), get_voter_info( "alice1111111" ) );

   BOOST_REQUIRE_EQUAL( core_sym::from_string("0.0000"), get_balance( "alice1111111" ) );

   //alice stakes for herself
   transfer( "flon", "alice1111111", core_sym::from_string("1000.0000"), "flon" );
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", "alice1111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );
   //now alice's stake should be equal to transferred from eosio + own stake
   total = get_total_stake("alice1111111");
   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("410.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("210.0000"), total["cpu_weight"].as<asset>());
   // REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("600.0000")), get_voter_info( "alice1111111" ) );

   //alice can unstake everything (including what was transferred)
   BOOST_REQUIRE_EQUAL( success(), unstake( "alice1111111", "alice1111111", core_sym::from_string("400.0000"), core_sym::from_string("200.0000") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );

   produce_block( fc::hours(3*24-1) );
   produce_blocks(1);
   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );
   //after 3 days funds should be released

   produce_block( fc::hours(1) );
   produce_blocks(1);

   BOOST_REQUIRE_EQUAL( success(), push_action( "alice1111111"_n, "refund"_n, mvo()("owner", "alice1111111") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("1300.0000"), get_balance( "alice1111111" ) );

   //stake should be equal to what was staked in constructor, voting power should be 0
   total = get_total_stake("alice1111111");
   BOOST_REQUIRE_EQUAL( core_sym::from_string("10.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("10.0000"), total["cpu_weight"].as<asset>());
   // REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("0.0000")), get_voter_info( "alice1111111" ) );

   // Now alice stakes to bob with transfer flag
   BOOST_REQUIRE_EQUAL( success(), stake_with_transfer( "alice1111111"_n, "bob111111111"_n, core_sym::from_string("100.0000"), core_sym::from_string("100.0000") ) );

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( fail_without_auth, eosio_system_tester ) try {
   cross_15_percent_threshold();

   issue_and_transfer( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );

   BOOST_REQUIRE_EQUAL( success(), stake( "flon", "alice1111111", core_sym::from_string("2000.0000"), core_sym::from_string("1000.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", "bob111111111", core_sym::from_string("10.0000"), core_sym::from_string("10.0000") ) );

   BOOST_REQUIRE_EQUAL( error("missing authority of alice1111111"),
                        push_action( "alice1111111"_n, "delegatebw"_n, mvo()
                                    ("from",     "alice1111111")
                                    ("receiver", "bob111111111")
                                    ("stake_net_quantity", core_sym::from_string("10.0000"))
                                    ("stake_cpu_quantity", core_sym::from_string("10.0000"))
                                    ("transfer", 0 )
                                    ,false
                        )
   );

   BOOST_REQUIRE_EQUAL( error("missing authority of alice1111111"),
                        push_action("alice1111111"_n, "undelegatebw"_n, mvo()
                                    ("from",     "alice1111111")
                                    ("receiver", "bob111111111")
                                    ("unstake_net_quantity", core_sym::from_string("200.0000"))
                                    ("unstake_cpu_quantity", core_sym::from_string("100.0000"))
                                    ("transfer", 0 )
                                    ,false
                        )
   );
   //REQUIRE_MATCHING_OBJECT( , get_voter_info( "alice1111111" ) );
} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( stake_negative, eosio_system_tester ) try {
   issue_and_transfer( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );

   BOOST_REQUIRE_EQUAL( wasm_assert_msg("must stake a positive amount"),
                        stake( "alice1111111", core_sym::from_string("-0.0001"), core_sym::from_string("0.0000") )
   );

   BOOST_REQUIRE_EQUAL( wasm_assert_msg("must stake a positive amount"),
                        stake( "alice1111111", core_sym::from_string("0.0000"), core_sym::from_string("-0.0001") )
   );

   BOOST_REQUIRE_EQUAL( wasm_assert_msg("must stake a positive amount"),
                        stake( "alice1111111", core_sym::from_string("00.0000"), core_sym::from_string("00.0000") )
   );

   BOOST_REQUIRE_EQUAL( wasm_assert_msg("must stake a positive amount"),
                        stake( "alice1111111", core_sym::from_string("0.0000"), core_sym::from_string("00.0000") )

   );

   BOOST_REQUIRE_EQUAL( true, get_voter_info( "alice1111111" ).is_null() );
} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( unstake_negative, eosio_system_tester ) try {
   issue_and_transfer( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );

   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", "bob111111111", core_sym::from_string("200.0001"), core_sym::from_string("100.0001") ) );

   auto total = get_total_stake( "bob111111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("210.0001"), total["net_weight"].as<asset>());
   // auto vinfo = get_voter_info("alice1111111" );
   // REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("300.0002") ), get_voter_info( "alice1111111" ) );


   BOOST_REQUIRE_EQUAL( wasm_assert_msg("must unstake a positive amount"),
                        unstake( "alice1111111", "bob111111111", core_sym::from_string("-1.0000"), core_sym::from_string("0.0000") )
   );

   BOOST_REQUIRE_EQUAL( wasm_assert_msg("must unstake a positive amount"),
                        unstake( "alice1111111", "bob111111111", core_sym::from_string("0.0000"), core_sym::from_string("-1.0000") )
   );

   //unstake all zeros
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("must unstake a positive amount"),
                        unstake( "alice1111111", "bob111111111", core_sym::from_string("0.0000"), core_sym::from_string("0.0000") )

   );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( unstake_more_than_at_stake, eosio_system_tester ) try {
   cross_15_percent_threshold();

   issue_and_transfer( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );

   auto total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("210.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("110.0000"), total["cpu_weight"].as<asset>());

   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );

   //trying to unstake more net bandwidth than at stake

   BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient staked net bandwidth"),
                        unstake( "alice1111111", core_sym::from_string("200.0001"), core_sym::from_string("0.0000") )
   );

   //trying to unstake more cpu bandwidth than at stake
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient staked cpu bandwidth"),
                        unstake( "alice1111111", core_sym::from_string("0.0000"), core_sym::from_string("100.0001") )

   );

   //check that nothing has changed
   total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("210.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("110.0000"), total["cpu_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );
} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( delegate_to_another_user, eosio_system_tester ) try {
   cross_15_percent_threshold();

   issue_and_transfer( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );

   BOOST_REQUIRE_EQUAL( success(), stake ( "alice1111111", "bob111111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );

   auto total = get_total_stake( "bob111111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("210.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("110.0000"), total["cpu_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );
   //all voting power goes to alice1111111
   // REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("300.0000") ), get_voter_info( "alice1111111" ) );
   //but not to bob111111111
   BOOST_REQUIRE_EQUAL( true, get_voter_info( "bob111111111" ).is_null() );

   //bob111111111 should not be able to unstake what was staked by alice1111111
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient staked cpu bandwidth"),
                        unstake( "bob111111111", core_sym::from_string("0.0000"), core_sym::from_string("10.0000") )

   );
   BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient staked net bandwidth"),
                        unstake( "bob111111111", core_sym::from_string("10.0000"),  core_sym::from_string("0.0000") )
   );

   issue_and_transfer( "carol1111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), stake( "carol1111111", "bob111111111", core_sym::from_string("20.0000"), core_sym::from_string("10.0000") ) );
   total = get_total_stake( "bob111111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("230.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("120.0000"), total["cpu_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("970.0000"), get_balance( "carol1111111" ) );
   // REQUIRE_MATCHING_OBJECT( voter( "carol1111111", core_sym::from_string("30.0000") ), get_voter_info( "carol1111111" ) );

   //alice1111111 should not be able to unstake money staked by carol1111111

   BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient staked net bandwidth"),
                        unstake( "alice1111111", "bob111111111", core_sym::from_string("2001.0000"), core_sym::from_string("1.0000") )
   );

   BOOST_REQUIRE_EQUAL( wasm_assert_msg("insufficient staked cpu bandwidth"),
                        unstake( "alice1111111", "bob111111111", core_sym::from_string("1.0000"), core_sym::from_string("101.0000") )

   );

   total = get_total_stake( "bob111111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("230.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("120.0000"), total["cpu_weight"].as<asset>());
   //balance should not change after unsuccessful attempts to unstake
   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );
   //voting power too
   // REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("300.0000") ), get_voter_info( "alice1111111" ) );
   // REQUIRE_MATCHING_OBJECT( voter( "carol1111111", core_sym::from_string("30.0000") ), get_voter_info( "carol1111111" ) );
   BOOST_REQUIRE_EQUAL( true, get_voter_info( "bob111111111" ).is_null() );
} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( stake_unstake_separate, eosio_system_tester ) try {
   cross_15_percent_threshold();

   issue_and_transfer( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("1000.0000"), get_balance( "alice1111111" ) );

   //everything at once
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", core_sym::from_string("10.0000"), core_sym::from_string("20.0000") ) );
   auto total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("20.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("30.0000"), total["cpu_weight"].as<asset>());

   //cpu
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", core_sym::from_string("100.0000"), core_sym::from_string("0.0000") ) );
   total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("120.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("30.0000"), total["cpu_weight"].as<asset>());

   //net
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", core_sym::from_string("0.0000"), core_sym::from_string("200.0000") ) );
   total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("120.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("230.0000"), total["cpu_weight"].as<asset>());

   //unstake cpu
   BOOST_REQUIRE_EQUAL( success(), unstake( "alice1111111", core_sym::from_string("100.0000"), core_sym::from_string("0.0000") ) );
   total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("20.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("230.0000"), total["cpu_weight"].as<asset>());

   //unstake net
   BOOST_REQUIRE_EQUAL( success(), unstake( "alice1111111", core_sym::from_string("0.0000"), core_sym::from_string("200.0000") ) );
   total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("20.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("30.0000"), total["cpu_weight"].as<asset>());
} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( adding_stake_partial_unstake, eosio_system_tester ) try {
   cross_15_percent_threshold();

   issue_and_transfer( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", "bob111111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );

   // REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("300.0000") ), get_voter_info( "alice1111111" ) );

   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", "bob111111111", core_sym::from_string("100.0000"), core_sym::from_string("50.0000") ) );

   auto total = get_total_stake( "bob111111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("310.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("160.0000"), total["cpu_weight"].as<asset>());
   // REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("450.0000") ), get_voter_info( "alice1111111" ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("550.0000"), get_balance( "alice1111111" ) );

   //unstake a share
   BOOST_REQUIRE_EQUAL( success(), unstake( "alice1111111", "bob111111111", core_sym::from_string("150.0000"), core_sym::from_string("75.0000") ) );

   total = get_total_stake( "bob111111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("160.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("85.0000"), total["cpu_weight"].as<asset>());
   // REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("225.0000") ), get_voter_info( "alice1111111" ) );

   //unstake more
   BOOST_REQUIRE_EQUAL( success(), unstake( "alice1111111", "bob111111111", core_sym::from_string("50.0000"), core_sym::from_string("25.0000") ) );
   total = get_total_stake( "bob111111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("110.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("60.0000"), total["cpu_weight"].as<asset>());
   // REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("150.0000") ), get_voter_info( "alice1111111" ) );

   //combined amount should be available only in 3 days
   produce_block( fc::days(2) );
   produce_blocks(1);
   BOOST_REQUIRE_EQUAL( core_sym::from_string("550.0000"), get_balance( "alice1111111" ) );
   produce_block( fc::days(1) );
   produce_blocks(1);
   BOOST_REQUIRE_EQUAL( success(), push_action( "alice1111111"_n, "refund"_n, mvo()("owner", "alice1111111") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("850.0000"), get_balance( "alice1111111" ) );

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( stake_from_refund, eosio_system_tester ) try {
   cross_15_percent_threshold();

   issue_and_transfer( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", "alice1111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );

   auto total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("210.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("110.0000"), total["cpu_weight"].as<asset>());

   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", "bob111111111", core_sym::from_string("50.0000"), core_sym::from_string("50.0000") ) );

   total = get_total_stake( "bob111111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("60.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("60.0000"), total["cpu_weight"].as<asset>());

   // REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("400.0000") ), get_voter_info( "alice1111111" ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("600.0000"), get_balance( "alice1111111" ) );

   //unstake a share
   BOOST_REQUIRE_EQUAL( success(), unstake( "alice1111111", "alice1111111", core_sym::from_string("100.0000"), core_sym::from_string("50.0000") ) );
   total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("110.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("60.0000"), total["cpu_weight"].as<asset>());
   // REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("250.0000") ), get_voter_info( "alice1111111" ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("600.0000"), get_balance( "alice1111111" ) );
   auto refund = get_refund_request( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("100.0000"), refund["net_amount"].as<asset>() );
   BOOST_REQUIRE_EQUAL( core_sym::from_string( "50.0000"), refund["cpu_amount"].as<asset>() );
   //XXX auto request_time = refund["request_time"].as_int64();

   //alice delegates to bob, should pull from liquid balance not refund
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", "bob111111111", core_sym::from_string("50.0000"), core_sym::from_string("50.0000") ) );
   total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("110.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("60.0000"), total["cpu_weight"].as<asset>());
   // REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("350.0000") ), get_voter_info( "alice1111111" ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("500.0000"), get_balance( "alice1111111" ) );
   refund = get_refund_request( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("100.0000"), refund["net_amount"].as<asset>() );
   BOOST_REQUIRE_EQUAL( core_sym::from_string( "50.0000"), refund["cpu_amount"].as<asset>() );

   //stake less than pending refund, entire amount should be taken from refund
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", "alice1111111", core_sym::from_string("50.0000"), core_sym::from_string("25.0000") ) );
   total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("160.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("85.0000"), total["cpu_weight"].as<asset>());
   refund = get_refund_request( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("50.0000"), refund["net_amount"].as<asset>() );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("25.0000"), refund["cpu_amount"].as<asset>() );
   //request time should stay the same
   //BOOST_REQUIRE_EQUAL( request_time, refund["request_time"].as_int64() );
   //balance should stay the same
   BOOST_REQUIRE_EQUAL( core_sym::from_string("500.0000"), get_balance( "alice1111111" ) );

   //stake exactly pending refund amount
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", "alice1111111", core_sym::from_string("50.0000"), core_sym::from_string("25.0000") ) );
   total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("210.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("110.0000"), total["cpu_weight"].as<asset>());
   //pending refund should be removed
   refund = get_refund_request( "alice1111111"_n );
   BOOST_TEST_REQUIRE( refund.is_null() );
   //balance should stay the same
   BOOST_REQUIRE_EQUAL( core_sym::from_string("500.0000"), get_balance( "alice1111111" ) );

   //create pending refund again
   BOOST_REQUIRE_EQUAL( success(), unstake( "alice1111111", "alice1111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );
   total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("10.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("10.0000"), total["cpu_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("500.0000"), get_balance( "alice1111111" ) );
   refund = get_refund_request( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("200.0000"), refund["net_amount"].as<asset>() );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("100.0000"), refund["cpu_amount"].as<asset>() );

   //stake more than pending refund
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", "alice1111111", core_sym::from_string("300.0000"), core_sym::from_string("200.0000") ) );
   total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("310.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("210.0000"), total["cpu_weight"].as<asset>());
   // REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("700.0000") ), get_voter_info( "alice1111111" ) );
   refund = get_refund_request( "alice1111111"_n );
   BOOST_TEST_REQUIRE( refund.is_null() );
   //200 core tokens should be taken from alice's account
   BOOST_REQUIRE_EQUAL( core_sym::from_string("300.0000"), get_balance( "alice1111111" ) );

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( stake_to_another_user_not_from_refund, eosio_system_tester ) try {
   cross_15_percent_threshold();

   issue_and_transfer( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );

   auto total = get_total_stake( "alice1111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("210.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("110.0000"), total["cpu_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );

   // REQUIRE_MATCHING_OBJECT( voter( "alice1111111", core_sym::from_string("300.0000") ), get_voter_info( "alice1111111" ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance( "alice1111111" ) );

   //unstake
   BOOST_REQUIRE_EQUAL( success(), unstake( "alice1111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );
   auto refund = get_refund_request( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("200.0000"), refund["net_amount"].as<asset>() );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("100.0000"), refund["cpu_amount"].as<asset>() );
   //auto orig_request_time = refund["request_time"].as_int64();

   //stake to another user
   BOOST_REQUIRE_EQUAL( success(), stake( "alice1111111", "bob111111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );
   total = get_total_stake( "bob111111111" );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("210.0000"), total["net_weight"].as<asset>());
   BOOST_REQUIRE_EQUAL( core_sym::from_string("110.0000"), total["cpu_weight"].as<asset>());
   //stake should be taken from alice's balance, and refund request should stay the same
   BOOST_REQUIRE_EQUAL( core_sym::from_string("400.0000"), get_balance( "alice1111111" ) );
   refund = get_refund_request( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("200.0000"), refund["net_amount"].as<asset>() );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("100.0000"), refund["cpu_amount"].as<asset>() );
   //BOOST_REQUIRE_EQUAL( orig_request_time, refund["request_time"].as_int64() );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
BOOST_AUTO_TEST_SUITE(eosio_system_producer_tests)

// Tests for voting
BOOST_FIXTURE_TEST_CASE( producer_register_unregister, eosio_system_tester ) try {
   issue_and_transfer( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );

   //fc::variant params = producer_parameters_example(1);
   auto key =  fc::crypto::public_key( std::string("EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV") ); // cspell:disable-line
   BOOST_REQUIRE_EQUAL( success(), push_action("alice1111111"_n, "regproducer"_n, mvo()
                                               ("producer",  "alice1111111")
                                               ("producer_key", key )
                                               ("url", "http://block.one")
                                               ("location", 1)
                        )
   );

   auto info = get_producer_info( "alice1111111" );
   BOOST_REQUIRE_EQUAL( "alice1111111", info["owner"].as_string() );
   BOOST_REQUIRE_EQUAL( 0, info["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( "http://block.one", info["url"].as_string() );

   //change parameters one by one to check for things like #3783
   //fc::variant params2 = producer_parameters_example(2);
   BOOST_REQUIRE_EQUAL( success(), push_action("alice1111111"_n, "regproducer"_n, mvo()
                                               ("producer",  "alice1111111")
                                               ("producer_key", key )
                                               ("url", "http://block.two")
                                               ("location", 1)
                        )
   );
   info = get_producer_info( "alice1111111" );
   BOOST_REQUIRE_EQUAL( "alice1111111", info["owner"].as_string() );
   BOOST_REQUIRE_EQUAL( key, fc::crypto::public_key(info["producer_key"].as_string()) );
   BOOST_REQUIRE_EQUAL( "http://block.two", info["url"].as_string() );
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
   cross_15_percent_threshold();

   BOOST_REQUIRE_EQUAL( control->active_producers().version, 0u );

   issue_and_transfer( "alice1111111"_n, core_sym::from_string("200000000.0000"),  config::system_account_name );
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
   BOOST_REQUIRE_EQUAL( success(), addvote( "alice1111111"_n, core_sym::from_string("100000000.0000"), core_sym::from_string("100000000.0000") ) );
   BOOST_REQUIRE_EQUAL( success(), vote( "alice1111111"_n, { "alice1111111"_n } ) );

   block_signing_private_keys.emplace(get_public_key("alice1111111"_n, "bs1"), get_private_key("alice1111111"_n, "bs1"));

   auto alice_prod_info = get_producer_info( "alice1111111"_n );
   wdump((alice_prod_info));
   BOOST_REQUIRE_EQUAL( alice_prod_info["is_active"], true );

   produce_block();
   produce_block( fc::minutes(2) );
   produce_blocks(config::producer_repetitions * 2);
   BOOST_REQUIRE_EQUAL( control->active_producers().version, 1u );
   produce_block();
   BOOST_REQUIRE_EQUAL( control->pending_block_producer(), "alice1111111"_n );
   produce_block();

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

   produce_block();
   produce_block( fc::minutes(2) );
   produce_blocks(config::producer_repetitions * 2);
   BOOST_REQUIRE_EQUAL( control->active_producers().version, 2u );
   produce_block();
   BOOST_REQUIRE_EQUAL( control->pending_block_producer(), "alice1111111"_n );
   produce_block();

} FC_LOG_AND_RETHROW()

// BOOST_FIXTURE_TEST_CASE( producer_wtmsig_transition, eosio_system_tester ) try {
//    cross_15_percent_threshold();

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

BOOST_FIXTURE_TEST_CASE( vote_for_producer, eosio_system_tester, * boost::unit_test::tolerance(1e+5) ) try {
   cross_15_percent_threshold();

   issue_and_transfer( "alice1111111", core_sym::from_string("1000.0000"),  config::system_account_name );
   fc::variant params = producer_parameters_example(1);
   BOOST_REQUIRE_EQUAL( success(), push_action( "alice1111111"_n, "regproducer"_n, mvo()
                                               ("producer",  "alice1111111")
                                               ("producer_key", get_public_key( "alice1111111"_n, "active") )
                                               ("url", "http://block.one")
                                               ("location", 0 )
                        )
   );
   auto prod = get_producer_info( "alice1111111" );
   BOOST_REQUIRE_EQUAL( "alice1111111", prod["owner"].as_string() );
   BOOST_REQUIRE_EQUAL( 0, prod["total_votes"].as_int64() );
   BOOST_REQUIRE_EQUAL( "http://block.one", prod["url"].as_string() );

   issue_and_transfer( "bob111111111", core_sym::from_string("2000.0000"),  config::system_account_name );
   issue_and_transfer( "carol1111111", core_sym::from_string("3000.0000"),  config::system_account_name );

   //bob111111111 makes stake
   BOOST_REQUIRE_EQUAL( success(), addvote( "bob111111111", core_sym::from_string("11.0000"), core_sym::from_string("0.1111") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("1988.8889"), get_balance( "bob111111111" ) );
   REQUIRE_MATCHING_OBJECT( voter( "bob111111111", core_sym::from_string("11.1111") ), get_voter_info( "bob111111111" ) );

   //bob111111111 votes for alice1111111
   BOOST_REQUIRE_EQUAL( success(), vote( "bob111111111"_n, { "alice1111111"_n } ) );

   //check that producer parameters stay the same after voting
   prod = get_producer_info( "alice1111111" );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("11.1111")) == prod["total_votes"].as_int64() );
   BOOST_REQUIRE_EQUAL( "alice1111111", prod["owner"].as_string() );
   BOOST_REQUIRE_EQUAL( "http://block.one", prod["url"].as_string() );

   //carol1111111 makes stake
   BOOST_REQUIRE_EQUAL( success(), addvote( "carol1111111", core_sym::from_string("22.0000"), core_sym::from_string("0.2222") ) );
   REQUIRE_MATCHING_OBJECT( voter( "carol1111111", core_sym::from_string("22.2222") ), get_voter_info( "carol1111111" ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("2977.7778"), get_balance( "carol1111111" ) );
   //carol1111111 votes for alice1111111
   BOOST_REQUIRE_EQUAL( success(), vote( "carol1111111"_n, { "alice1111111"_n } ) );

   //new stake votes be added to alice1111111's total_votes
   prod = get_producer_info( "alice1111111" );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("33.3333")) == prod["total_votes"].as_int64() );

   //bob111111111 increases his stake
   BOOST_REQUIRE_EQUAL( success(), addvote( "bob111111111", core_sym::from_string("33.0000"), core_sym::from_string("0.3333") ) );
   //alice1111111 stake with transfer to bob111111111
   BOOST_REQUIRE_EQUAL( success(), addvote( "alice1111111"_n, "bob111111111"_n, core_sym::from_string("22.0000"), core_sym::from_string("0.2222") ) );
   //should increase alice1111111's total_votes
   prod = get_producer_info( "alice1111111" );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("88.8888")) == prod["total_votes"].as_int64() );

   //carol1111111 unstakes part of the stake
   BOOST_REQUIRE_EQUAL( success(), subvote( "carol1111111", core_sym::from_string("2.0000"), core_sym::from_string("0.0002")/*"2.0000 EOS", "0.0002 EOS"*/ ) );

   //should decrease alice1111111's total_votes
   prod = get_producer_info( "alice1111111" );
   wdump((prod));
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("86.8886")) == prod["total_votes"].as_int64() );

   //bob111111111 revokes his vote
   BOOST_REQUIRE_EQUAL( success(), vote( "bob111111111"_n, vector<account_name>() ) );

   //should decrease alice1111111's total_votes
   prod = get_producer_info( "alice1111111" );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("20.2220")) == prod["total_votes"].as_int64() );
   //but core asset should still be at stake
   BOOST_REQUIRE_EQUAL( core_sym::from_string("1955.5556"), get_balance( "bob111111111" ) );

   produce_block( fc::days(3) );
   BOOST_REQUIRE_EQUAL( success(), push_action( "carol1111111"_n, "voterefund"_n, mvo()("owner", "carol1111111") ) );
   //carol1111111 unstakes rest of core asset
   BOOST_REQUIRE_EQUAL( success(), subvote( "carol1111111", core_sym::from_string("20.0000"), core_sym::from_string("0.2220") ) );
   //should decrease alice1111111's total_votes to zero
   prod = get_producer_info( "alice1111111" );
   BOOST_TEST_REQUIRE( 0.0 == prod["total_votes"].as_int64() );

   //carol1111111 should receive funds in 3 days
   produce_block( fc::days(3) );
   produce_block();

   // do a bid refund for carol
   BOOST_REQUIRE_EQUAL( success(), push_action( "carol1111111"_n, "voterefund"_n, mvo()("owner", "carol1111111") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string("3000.0000"), get_balance( "carol1111111" ) );

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
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("20.0005")) == alice_info["total_votes"].as_int64() );
   auto bob_info = get_producer_info( "bob111111111" );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("20.0005")) == bob_info["total_votes"].as_int64() );

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
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("22.0005")) == alice_info["total_votes"].as_int64() );

   bob_info = get_producer_info( "bob111111111" );
   BOOST_TEST_REQUIRE( stake2votes(core_sym::from_string("2.0000")) == bob_info["total_votes"].as_int64() );

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

   const asset large_asset = core_sym::from_string("80.0000");
   create_account_with_resources( "producvotera"_n, config::system_account_name, core_sym::from_string("1.0000"), false, large_asset, large_asset );
   create_account_with_resources( "producvoterb"_n, config::system_account_name, core_sym::from_string("1.0000"), false, large_asset, large_asset );
   create_account_with_resources( "producvoterc"_n, config::system_account_name, core_sym::from_string("1.0000"), false, large_asset, large_asset );

   // create accounts {defproducera, defproducerb, ..., defproducerz} and register as producers
   std::vector<account_name> producer_names;
   producer_names.reserve('z' - 'a' + 1);
   const std::string root("defproducer");
   for ( char c = 'a'; c <= 'z'; ++c ) {
      producer_names.emplace_back(root + std::string(1, c));
   }
   setup_producer_accounts(producer_names);

   for (auto a:producer_names)
      regproducer(a);

   produce_block(fc::hours(24));

   BOOST_REQUIRE_EQUAL(0, get_producer_info( producer_names.front() )["total_votes"].as<double>());
   BOOST_REQUIRE_EQUAL(0, get_producer_info( producer_names.back() )["total_votes"].as<double>());


   transfer(config::system_account_name, "producvotera", core_sym::from_string("200000000.0000"), config::system_account_name);
   BOOST_REQUIRE_EQUAL(success(), addvote("producvotera", core_sym::from_string("70000000.0000"), core_sym::from_string("70000000.0000") ));
   BOOST_REQUIRE_EQUAL(success(), vote( "producvotera"_n, vector<account_name>(producer_names.begin(), producer_names.begin()+10)));
   BOOST_CHECK_EQUAL( wasm_assert_msg( "cannot subvote until the chain is activated (at least 15% of all tokens participate in voting)" ),
                      subvote( "producvotera", core_sym::from_string("50.0000"), core_sym::from_string("50.0000") ) );

   // give a chance for everyone to produce blocks
   {
      produce_blocks(21 * 12);
      bool all_21_produced = true;
      for (uint32_t i = 0; i < 21; ++i) {
         if (0 == get_producer_info(producer_names[i])["unpaid_blocks"].as<uint32_t>()) {
            all_21_produced= false;
         }
      }
      bool rest_didnt_produce = true;
      for (uint32_t i = 21; i < producer_names.size(); ++i) {
         if (0 < get_producer_info(producer_names[i])["unpaid_blocks"].as<uint32_t>()) {
            rest_didnt_produce = false;
         }
      }
      BOOST_REQUIRE_EQUAL(false, all_21_produced);
      BOOST_REQUIRE_EQUAL(true, rest_didnt_produce);
   }

   //TODO: producer rewards
   #ifdef ENABLED_PRODUCER_REWARD
   {
      const char* claimrewards_activation_error_message = "cannot claim rewards until the chain is activated (at least 15% of all tokens participate in voting)";
      BOOST_CHECK_EQUAL(0u, get_global_state()["total_unpaid_blocks"].as<uint32_t>());
      BOOST_REQUIRE_EQUAL(wasm_assert_msg( claimrewards_activation_error_message ),
                          push_action(producer_names.front(), "claimrewards"_n, mvo()("owner", producer_names.front())));
      BOOST_REQUIRE_EQUAL(0, get_balance(producer_names.front()).get_amount());
      BOOST_REQUIRE_EQUAL(wasm_assert_msg( claimrewards_activation_error_message ),
                          push_action(producer_names.back(), "claimrewards"_n, mvo()("owner", producer_names.back())));
      BOOST_REQUIRE_EQUAL(0, get_balance(producer_names.back()).get_amount());
   }

   // stake across 15% boundary
   transfer(config::system_account_name, "producvoterb", core_sym::from_string("100000000.0000"), config::system_account_name);
   BOOST_REQUIRE_EQUAL(success(), addvote("producvoterb", core_sym::from_string("4000000.0000"), core_sym::from_string("4000000.0000")));
   transfer(config::system_account_name, "producvoterc", core_sym::from_string("100000000.0000"), config::system_account_name);
   BOOST_REQUIRE_EQUAL(success(), stake("producvoterc", core_sym::from_string("2000000.0000"), core_sym::from_string("2000000.0000")));

   BOOST_REQUIRE_EQUAL(success(), vote( "producvoterb"_n, vector<account_name>(producer_names.begin(), producer_names.begin()+21)));
   BOOST_REQUIRE_EQUAL(success(), vote( "producvoterc"_n, vector<account_name>(producer_names.begin(), producer_names.end())));

   int retries = 50;
   while (produce_block()->producer == config::system_account_name && --retries);
   BOOST_REQUIRE(retries > 0);

   // give a chance for everyone to produce blocks
   {
      produce_blocks(21 * 12);
      bool all_21_produced = true;
      for (uint32_t i = 0; i < 21; ++i) {
         if (0 == get_producer_info(producer_names[i])["unpaid_blocks"].as<uint32_t>()) {
            all_21_produced= false;
         }
      }
      bool rest_didnt_produce = true;
      for (uint32_t i = 21; i < producer_names.size(); ++i) {
         if (0 < get_producer_info(producer_names[i])["unpaid_blocks"].as<uint32_t>()) {
            rest_didnt_produce = false;
         }
      }
      BOOST_REQUIRE_EQUAL(true, all_21_produced);
      BOOST_REQUIRE_EQUAL(true, rest_didnt_produce);
      BOOST_REQUIRE_EQUAL(success(),
                          push_action(producer_names.front(), "claimrewards"_n, mvo()("owner", producer_names.front())));
      BOOST_REQUIRE(0 < get_balance(producer_names.front()).get_amount());
   }

   BOOST_CHECK_EQUAL( success(), subvote( "producvotera", core_sym::from_string("50.0000"), core_sym::from_string("50.0000") ) );
   #endif//ENABLE_VOTE_PROXY

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

BOOST_FIXTURE_TEST_CASE( elect_producers /*_and_parameters*/, eosio_system_tester ) try {
   create_accounts_with_resources( {  "defproducer1"_n, "defproducer2"_n, "defproducer3"_n } );
   BOOST_REQUIRE_EQUAL( success(), regproducer( "defproducer1"_n, 1) );
   BOOST_REQUIRE_EQUAL( success(), regproducer( "defproducer2"_n, 2) );
   BOOST_REQUIRE_EQUAL( success(), regproducer( "defproducer3"_n, 3) );

   //stake more than 15% of total EOS supply to activate chain
   transfer( "flon", "alice1111111", core_sym::from_string("600000000.0000"), "flon" );
   BOOST_REQUIRE_EQUAL( success(), addvote( "alice1111111", "alice1111111", core_sym::from_string("300000000.0000"), core_sym::from_string("300000000.0000") ) );
   //vote for producers
   BOOST_REQUIRE_EQUAL( success(), vote( "alice1111111"_n, { "defproducer1"_n } ) );
   produce_blocks(250);
   auto producer_schedule = control->active_producers();
   BOOST_REQUIRE_EQUAL( 1u, producer_schedule.producers.size() );
   BOOST_REQUIRE_EQUAL( name("defproducer1"), producer_schedule.producers[0].producer_name );

   //auto config = config_to_variant( control->get_global_properties().configuration );
   //auto prod1_config = testing::filter_fields( config, producer_parameters_example( 1 ) );
   //REQUIRE_EQUAL_OBJECTS(prod1_config, config);

   // elect 2 producers
   issue_and_transfer( "bob111111111", core_sym::from_string("80000.0000"),  config::system_account_name );
   ilog("stake");
   BOOST_REQUIRE_EQUAL( success(), addvote( "bob111111111", core_sym::from_string("40000.0000"), core_sym::from_string("40000.0000") ) );
   ilog("start vote");
   BOOST_REQUIRE_EQUAL( success(), vote( "bob111111111"_n, { "defproducer2"_n } ) );
   ilog(".");
   produce_blocks(250);
   producer_schedule = control->active_producers();
   BOOST_REQUIRE_EQUAL( 2u, producer_schedule.producers.size() );
   BOOST_REQUIRE_EQUAL( name("defproducer1"), producer_schedule.producers[0].producer_name );
   BOOST_REQUIRE_EQUAL( name("defproducer2"), producer_schedule.producers[1].producer_name );
   //config = config_to_variant( control->get_global_properties().configuration );
   //auto prod2_config = testing::filter_fields( config, producer_parameters_example( 2 ) );
   //REQUIRE_EQUAL_OBJECTS(prod2_config, config);

   // elect 3 producers
   BOOST_REQUIRE_EQUAL( success(), vote( "bob111111111"_n, { "defproducer2"_n, "defproducer3"_n } ) );
   produce_blocks(250);
   producer_schedule = control->active_producers();
   BOOST_REQUIRE_EQUAL( 3u, producer_schedule.producers.size() );
   BOOST_REQUIRE_EQUAL( name("defproducer1"), producer_schedule.producers[0].producer_name );
   BOOST_REQUIRE_EQUAL( name("defproducer2"), producer_schedule.producers[1].producer_name );
   BOOST_REQUIRE_EQUAL( name("defproducer3"), producer_schedule.producers[2].producer_name );
   //config = config_to_variant( control->get_global_properties().configuration );
   //REQUIRE_EQUAL_OBJECTS(prod2_config, config);

   // try to go back to 2 producers and fail
   BOOST_REQUIRE_EQUAL( success(), vote( "bob111111111"_n, { "defproducer3"_n } ) );
   produce_blocks(250);
   producer_schedule = control->active_producers();
   BOOST_REQUIRE_EQUAL( 3u, producer_schedule.producers.size() );

   // The test below is invalid now, producer schedule is not updated if there are
   // fewer producers in the new schedule
   /*
   BOOST_REQUIRE_EQUAL( 2, producer_schedule.size() );
   BOOST_REQUIRE_EQUAL( name("defproducer1"), producer_schedule[0].producer_name );
   BOOST_REQUIRE_EQUAL( name("defproducer3"), producer_schedule[1].producer_name );
   //config = config_to_variant( control->get_global_properties().configuration );
   //auto prod3_config = testing::filter_fields( config, producer_parameters_example( 3 ) );
   //REQUIRE_EQUAL_OBJECTS(prod3_config, config);
   */

} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_SUITE_END()
BOOST_AUTO_TEST_SUITE(eosio_system_name_tests)

BOOST_FIXTURE_TEST_CASE( buyname, eosio_system_tester ) try {
   create_accounts_with_resources( { "dan"_n, "sam"_n } );
   transfer( config::system_account_name, "dan", core_sym::from_string( "10000.0000" ) );
   transfer( config::system_account_name, "sam", core_sym::from_string( "10000.0000" ) );
   addvote( config::system_account_name, "sam"_n, core_sym::from_string( "80000000.0000" ), core_sym::from_string( "80000000.0000" ) );
   addvote( config::system_account_name, "dan"_n, core_sym::from_string( "80000000.0000" ), core_sym::from_string( "80000000.0000" ) );

   regproducer( config::system_account_name );
   BOOST_REQUIRE_EQUAL( success(), vote( "sam"_n, { config::system_account_name } ) );
   // wait 14 days after min required amount has been staked
   produce_block( fc::days(7) );
   BOOST_REQUIRE_EQUAL( success(), vote( "dan"_n, { config::system_account_name } ) );
   produce_block( fc::days(7) );
   produce_block();

   BOOST_REQUIRE_EXCEPTION( create_accounts_with_resources( { "fail"_n }, "dan"_n ), // dan shouldn't be able to create fail
                            eosio_assert_message_exception, eosio_assert_message_is( "no active bid for name" ) );
   bidname( "dan", "nofail", core_sym::from_string( "1.0000" ) );
   BOOST_REQUIRE_EQUAL( "assertion failure with message: must increase bid by 10%", bidname( "sam", "nofail", core_sym::from_string( "1.0000" ) )); // didn't increase bid by 10%
   BOOST_REQUIRE_EQUAL( success(), bidname( "sam", "nofail", core_sym::from_string( "2.0000" ) )); // didn't increase bid by 10%
   produce_block( fc::days(1) );
   produce_block();

   BOOST_REQUIRE_EXCEPTION( create_accounts_with_resources( { "nofail"_n }, "dan"_n ), // dan shoudn't be able to do this, sam won
                            eosio_assert_message_exception, eosio_assert_message_is( "only highest bidder can claim" ) );
   //wlog( "verify sam can create nofail" );
   create_accounts_with_resources( { "nofail"_n }, "sam"_n ); // sam should be able to do this, he won the bid
   //wlog( "verify nofail can create test.nofail" );
   transfer( "flon", "nofail", core_sym::from_string( "1000.0000" ) );
   create_accounts_with_resources( { "test.nofail"_n }, "nofail"_n ); // only nofail can create test.nofail
   //wlog( "verify dan cannot create test.fail" );
   BOOST_REQUIRE_EXCEPTION( create_accounts_with_resources( { "test.fail"_n }, "dan"_n ), // dan shouldn't be able to do this
                            eosio_assert_message_exception, eosio_assert_message_is( "only suffix may create this account" ) );

   create_accounts_with_resources( { "goodgoodgood"_n }, "dan"_n ); /// 12 char names should succeed
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( bid_invalid_names, eosio_system_tester ) try {
   create_accounts_with_resources( { "dan"_n } );

   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "you can only bid on top-level suffix" ),
                        bidname( "dan", "abcdefg.12345", core_sym::from_string( "1.0000" ) ) );

   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "the empty name is not a valid account name to bid on" ),
                        bidname( "dan", "", core_sym::from_string( "1.0000" ) ) );

   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "13 character names are not valid account names to bid on" ),
                        bidname( "dan", "abcdefgh12345", core_sym::from_string( "1.0000" ) ) );

   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "accounts with 12 character names and no dots can be created without bidding required" ),
                        bidname( "dan", "abcdefg12345", core_sym::from_string( "1.0000" ) ) );

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( multiple_namebids, eosio_system_tester ) try {

   const std::string not_closed_message("auction for name is not closed yet");

   std::vector<account_name> accounts = { "alice"_n, "bob"_n, "carl"_n, "david"_n, "eve"_n };
   create_accounts_with_resources( accounts );
   for ( const auto& a: accounts ) {
      transfer( config::system_account_name, a, core_sym::from_string( "10000.0000" ) );
      BOOST_REQUIRE_EQUAL( core_sym::from_string( "10000.0000" ), get_balance(a) );
   }
   create_accounts_with_resources( { "producer"_n } );
   BOOST_REQUIRE_EQUAL( success(), regproducer( "producer"_n ) );

   produce_block();
   // stake but not enough to go live
   addvote( config::system_account_name, "bob"_n,  core_sym::from_string( "35000000.0000" ), core_sym::from_string( "35000000.0000" ) );
   addvote( config::system_account_name, "carl"_n, core_sym::from_string( "35000000.0000" ), core_sym::from_string( "35000000.0000" ) );
   BOOST_REQUIRE_EQUAL( success(), vote( "bob"_n, { "producer"_n } ) );
   BOOST_REQUIRE_EQUAL( success(), vote( "carl"_n, { "producer"_n } ) );

   // start bids
   bidname( "bob",  "prefa", core_sym::from_string("1.0003") );
   BOOST_REQUIRE_EQUAL( core_sym::from_string( "9998.9997" ), get_balance("bob") );
   bidname( "bob",  "prefb", core_sym::from_string("1.0000") );
   bidname( "bob",  "prefc", core_sym::from_string("1.0000") );
   BOOST_REQUIRE_EQUAL( core_sym::from_string( "9996.9997" ), get_balance("bob") );

   bidname( "carl", "prefd", core_sym::from_string("1.0000") );
   bidname( "carl", "prefe", core_sym::from_string("1.0000") );
   BOOST_REQUIRE_EQUAL( core_sym::from_string( "9998.0000" ), get_balance("carl") );

   BOOST_REQUIRE_EQUAL( error("assertion failure with message: account is already highest bidder"),
                        bidname( "bob", "prefb", core_sym::from_string("1.1001") ) );
   BOOST_REQUIRE_EQUAL( error("assertion failure with message: must increase bid by 10%"),
                        bidname( "alice", "prefb", core_sym::from_string("1.0999") ) );
   BOOST_REQUIRE_EQUAL( core_sym::from_string( "9996.9997" ), get_balance("bob") );
   BOOST_REQUIRE_EQUAL( core_sym::from_string( "10000.0000" ), get_balance("alice") );


   // alice outbids bob on prefb
   {
      const asset initial_names_balance = get_balance("flon.names"_n);
      BOOST_REQUIRE_EQUAL( success(),
                           bidname( "alice", "prefb", core_sym::from_string("1.1001") ) );
      // refund bob's failed bid on prefb
      BOOST_REQUIRE_EQUAL( success(), push_action( "bob"_n, "bidrefund"_n, mvo()("bidder","bob")("newname", "prefb") ) );
      BOOST_REQUIRE_EQUAL( core_sym::from_string( "9997.9997" ), get_balance("bob") );
      BOOST_REQUIRE_EQUAL( core_sym::from_string( "9998.8999" ), get_balance("alice") );
      BOOST_REQUIRE_EQUAL( initial_names_balance + core_sym::from_string("0.1001"), get_balance("flon.names"_n) );
   }

   // david outbids carl on prefd
   {
      BOOST_REQUIRE_EQUAL( core_sym::from_string( "9998.0000" ), get_balance("carl") );
      BOOST_REQUIRE_EQUAL( core_sym::from_string( "10000.0000" ), get_balance("david") );
      BOOST_REQUIRE_EQUAL( success(),
                           bidname( "david", "prefd", core_sym::from_string("1.9900") ) );
      // refund carls's failed bid on prefd
      BOOST_REQUIRE_EQUAL( success(), push_action( "carl"_n, "bidrefund"_n, mvo()("bidder","carl")("newname", "prefd") ) );
      BOOST_REQUIRE_EQUAL( core_sym::from_string( "9999.0000" ), get_balance("carl") );
      BOOST_REQUIRE_EQUAL( core_sym::from_string( "9998.0100" ), get_balance("david") );
   }

   // eve outbids carl on prefe
   {
      BOOST_REQUIRE_EQUAL( success(),
                           bidname( "eve", "prefe", core_sym::from_string("1.7200") ) );
   }

   produce_block( fc::days(14) );
   produce_block();

   // highest bid is from david for prefd but no bids can be closed yet
   BOOST_REQUIRE_EXCEPTION( create_account_with_resources( "prefd"_n, "david"_n ),
                            fc::exception, fc_assert_exception_message_is( not_closed_message ) );

   // stake enough to go above the 15% threshold
   addvote( config::system_account_name, "alice"_n, core_sym::from_string( "10000000.0000" ), core_sym::from_string( "10000000.0000" ) );
   BOOST_REQUIRE_EQUAL(0u, get_producer_info("producer")["unpaid_blocks"].as<uint32_t>());
   BOOST_REQUIRE_EQUAL( success(), vote( "alice"_n, { "producer"_n } ) );

   // need to wait for 14 days after going live
   produce_blocks(10);
   produce_block( fc::days(2) );
   produce_blocks( 10 );
   BOOST_REQUIRE_EXCEPTION( create_account_with_resources( "prefd"_n, "david"_n ),
                            fc::exception, fc_assert_exception_message_is( not_closed_message ) );
   // it's been 14 days, auction for prefd has been closed
   produce_block( fc::days(12) );
   create_account_with_resources( "prefd"_n, "david"_n );
   produce_blocks(2);
   produce_block( fc::hours(23) );
   // auctions for prefa, prefb, prefc, prefe haven't been closed
   BOOST_REQUIRE_EXCEPTION( create_account_with_resources( "prefa"_n, "bob"_n ),
                            fc::exception, fc_assert_exception_message_is( not_closed_message ) );
   BOOST_REQUIRE_EXCEPTION( create_account_with_resources( "prefb"_n, "alice"_n ),
                            fc::exception, fc_assert_exception_message_is( not_closed_message ) );
   BOOST_REQUIRE_EXCEPTION( create_account_with_resources( "prefc"_n, "bob"_n ),
                            fc::exception, fc_assert_exception_message_is( not_closed_message ) );
   BOOST_REQUIRE_EXCEPTION( create_account_with_resources( "prefe"_n, "eve"_n ),
                            fc::exception, fc_assert_exception_message_is( not_closed_message ) );
   // attemp to create account with no bid
   BOOST_REQUIRE_EXCEPTION( create_account_with_resources( "prefg"_n, "alice"_n ),
                            fc::exception, fc_assert_exception_message_is( "no active bid for name" ) );
   // changing highest bid pushes auction closing time by 24 hours
   BOOST_REQUIRE_EQUAL( success(),
                        bidname( "eve",  "prefb", core_sym::from_string("2.1880") ) );

   produce_block( fc::hours(22) );
   produce_blocks(2);

   BOOST_REQUIRE_EXCEPTION( create_account_with_resources( "prefb"_n, "eve"_n ),
                            fc::exception, fc_assert_exception_message_is( not_closed_message ) );
   // but changing a bid that is not the highest does not push closing time
   BOOST_REQUIRE_EQUAL( success(),
                        bidname( "carl", "prefe", core_sym::from_string("2.0980") ) );
   produce_block( fc::hours(2) );
   produce_blocks(2);
   // bid for prefb has closed, only highest bidder can claim
   BOOST_REQUIRE_EXCEPTION( create_account_with_resources( "prefb"_n, "alice"_n ),
                            eosio_assert_message_exception, eosio_assert_message_is( "only highest bidder can claim" ) );
   BOOST_REQUIRE_EXCEPTION( create_account_with_resources( "prefb"_n, "carl"_n ),
                            eosio_assert_message_exception, eosio_assert_message_is( "only highest bidder can claim" ) );
   create_account_with_resources( "prefb"_n, "eve"_n );

   BOOST_REQUIRE_EXCEPTION( create_account_with_resources( "prefe"_n, "carl"_n ),
                            fc::exception, fc_assert_exception_message_is( not_closed_message ) );
   produce_block();
   produce_block( fc::hours(24) );
   // by now bid for prefe has closed
   create_account_with_resources( "prefe"_n, "carl"_n );
   // prefe can now create *.prefe
   BOOST_REQUIRE_EXCEPTION( create_account_with_resources( "xyz.prefe"_n, "carl"_n ),
                            fc::exception, fc_assert_exception_message_is("only suffix may create this account") );
   transfer( config::system_account_name, "prefe"_n, core_sym::from_string("10000.0000") );
   create_account_with_resources( "xyz.prefe"_n, "prefe"_n );

   // other auctions haven't closed
   BOOST_REQUIRE_EXCEPTION( create_account_with_resources( "prefa"_n, "bob"_n ),
                            fc::exception, fc_assert_exception_message_is( not_closed_message ) );

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( namebid_pending_winner, eosio_system_tester ) try {
   cross_15_percent_threshold();
   produce_block( fc::hours(14*24) );    //wait 14 day for name auction activation
   transfer( config::system_account_name, "alice1111111"_n, core_sym::from_string("10000.0000") );
   transfer( config::system_account_name, "bob111111111"_n, core_sym::from_string("10000.0000") );

   BOOST_REQUIRE_EQUAL( success(), bidname( "alice1111111", "prefa", core_sym::from_string( "50.0000" ) ));
   BOOST_REQUIRE_EQUAL( success(), bidname( "bob111111111", "prefb", core_sym::from_string( "30.0000" ) ));
   produce_block( fc::hours(100) ); //should close "perfa"
   produce_block( fc::hours(100) ); //should close "perfb"

   //despite "perfa" account hasn't been created, we should be able to create "perfb" account
   create_account_with_resources( "prefb"_n, "bob111111111"_n );
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
      BOOST_REQUIRE_EQUAL(success(), push_action(producer_names[voted_out_index], "claimrewards"_n, mvo()("owner", producer_names[voted_out_index])));
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

BOOST_FIXTURE_TEST_CASE( setram_effect, eosio_system_tester ) try {

   const asset net = core_sym::from_string("8.0000");
   const asset cpu = core_sym::from_string("8.0000");
   std::vector<account_name> accounts = { "aliceaccount"_n, "bobbyaccount"_n };
   for (const auto& a: accounts) {
      create_account_with_resources(a, config::system_account_name, core_sym::from_string("1.0000"), false, net, cpu);
   }

   {
      const auto name_a = accounts[0];
      transfer( config::system_account_name, name_a, core_sym::from_string("1000.0000") );
      BOOST_REQUIRE_EQUAL( core_sym::from_string("1000.0000"), get_balance(name_a) );
      const uint64_t init_bytes_a = get_total_stake(name_a)["ram_bytes"].as_uint64();
      BOOST_REQUIRE_EQUAL( success(), buyram( name_a, name_a, core_sym::from_string("300.0000") ) );
      BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance(name_a) );
      const uint64_t bought_bytes_a = get_total_stake(name_a)["ram_bytes"].as_uint64() - init_bytes_a;

      // after buying and selling balance should be 700 + 300 * 0.995 * 0.995 = 997.0075 (actually 997.0074 due to rounding fees up)
      BOOST_REQUIRE_EQUAL( success(), sellram(name_a, bought_bytes_a ) );
      BOOST_REQUIRE_EQUAL( core_sym::from_string("997.0074"), get_balance(name_a) );
   }

   {
      const auto name_b = accounts[1];
      transfer( config::system_account_name, name_b, core_sym::from_string("1000.0000") );
      BOOST_REQUIRE_EQUAL( core_sym::from_string("1000.0000"), get_balance(name_b) );
      const uint64_t init_bytes_b = get_total_stake(name_b)["ram_bytes"].as_uint64();
      // name_b buys ram at current price
      BOOST_REQUIRE_EQUAL( success(), buyram( name_b, name_b, core_sym::from_string("300.0000") ) );
      BOOST_REQUIRE_EQUAL( core_sym::from_string("700.0000"), get_balance(name_b) );
      const uint64_t bought_bytes_b = get_total_stake(name_b)["ram_bytes"].as_uint64() - init_bytes_b;

      // increase max_ram_size, ram bought by name_b loses part of its value
      BOOST_REQUIRE_EQUAL( wasm_assert_msg("ram may only be increased"),
                           push_action(config::system_account_name, "setram"_n, mvo()("max_ram_size", 64ll*1024 * 1024 * 1024)) );
      BOOST_REQUIRE_EQUAL( error("missing authority of flon"),
                           push_action(name_b, "setram"_n, mvo()("max_ram_size", 80ll*1024 * 1024 * 1024)) );
      BOOST_REQUIRE_EQUAL( success(),
                           push_action(config::system_account_name, "setram"_n, mvo()("max_ram_size", 80ll*1024 * 1024 * 1024)) );

      BOOST_REQUIRE_EQUAL( success(), sellram(name_b, bought_bytes_b ) );
      BOOST_REQUIRE( core_sym::from_string("900.0000") < get_balance(name_b) );
      BOOST_REQUIRE( core_sym::from_string("950.0000") > get_balance(name_b) );
   }

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( eosioram_ramusage, eosio_system_tester ) try {
   BOOST_REQUIRE_EQUAL( core_sym::from_string("0.0000"), get_balance( "alice1111111" ) );
   transfer( "flon", "alice1111111", core_sym::from_string("1000.0000"), "flon" );
   BOOST_REQUIRE_EQUAL( success(), stake( "flon", "alice1111111", core_sym::from_string("200.0000"), core_sym::from_string("100.0000") ) );

   // const asset initial_ram_balance = get_balance("flon.ram"_n);
   // const asset initial_fees_balance = get_balance("flon.fees"_n);
   BOOST_REQUIRE_EQUAL( success(), buyram( "alice1111111", "alice1111111", core_sym::from_string("1000.0000") ) );

   BOOST_REQUIRE_EQUAL( false, get_row_by_account( "flon.token"_n, "alice1111111"_n, "accounts"_n, account_name(symbol{CORE_SYM}.to_symbol_code()) ).empty() );

   //remove row
   base_tester::push_action( "flon.token"_n, "close"_n, "alice1111111"_n, mvo()
                             ( "owner", "alice1111111" )
                             ( "symbol", symbol{CORE_SYM} )
   );
   BOOST_REQUIRE_EQUAL( true, get_row_by_account( "flon.token"_n, "alice1111111"_n, "accounts"_n, account_name(symbol{CORE_SYM}.to_symbol_code()) ).empty() );

   auto rlm = control->get_resource_limits_manager();
   auto eosioram_ram_usage = rlm.get_account_ram_usage("flon.ram"_n);
   auto alice_ram_usage = rlm.get_account_ram_usage("alice1111111"_n);

   BOOST_REQUIRE_EQUAL( success(), sellram( "alice1111111", 2048 ) );

   //make sure that ram was billed to alice, not to flon.ram
   BOOST_REQUIRE_EQUAL( true, alice_ram_usage < rlm.get_account_ram_usage("alice1111111"_n) );
   BOOST_REQUIRE_EQUAL( eosioram_ram_usage, rlm.get_account_ram_usage("flon.ram"_n) );

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

BOOST_FIXTURE_TEST_CASE( change_limited_account_back_to_unlimited, eosio_system_tester ) try {
   BOOST_REQUIRE( get_total_stake( "flon" ).is_null() );

   transfer( "flon"_n, "alice1111111"_n, core_sym::from_string("1.0000") );

   auto error_msg = stake( "alice1111111"_n, "flon"_n, core_sym::from_string("0.0000"), core_sym::from_string("1.0000") );
   auto semicolon_pos = error_msg.find(';');

   BOOST_REQUIRE_EQUAL( error("account flon has insufficient ram"),
                        error_msg.substr(0, semicolon_pos) );

   int64_t ram_bytes_needed = 0;
   {
      std::istringstream s( error_msg );
      s.seekg( semicolon_pos + 7, std::ios_base::beg );
      s >> ram_bytes_needed;
      ram_bytes_needed += 256; // enough room to cover total_resources_table
   }

   push_action( "flon"_n, "setalimits"_n, mvo()
                                          ("account", "flon")
                                          ("ram_bytes", ram_bytes_needed)
                                          ("net_weight", -1)
                                          ("cpu_weight", -1)
              );

   stake( "alice1111111"_n, "flon"_n, core_sym::from_string("0.0000"), core_sym::from_string("1.0000") );

   REQUIRE_MATCHING_OBJECT( get_total_stake( "flon" ), mvo()
      ("owner", "flon")
      ("net_weight", core_sym::from_string("0.0000"))
      ("cpu_weight", core_sym::from_string("1.0000"))
      ("ram_bytes",  0)
   );

   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "only supports unlimited accounts" ),
                        push_action( "flon"_n, "setalimits"_n, mvo()
                                          ("account", "flon")
                                          ("ram_bytes", ram_bytes_needed)
                                          ("net_weight", -1)
                                          ("cpu_weight", -1)
                        )
   );

   BOOST_REQUIRE_EQUAL( error( "transaction net usage is too high: 128 > 0" ),
                        push_action( "flon"_n, "setalimits"_n, mvo()
                           ("account", "flon.saving")
                           ("ram_bytes", -1)
                           ("net_weight", -1)
                           ("cpu_weight", -1)
                        )
   );

   BOOST_REQUIRE_EQUAL( success(),
                        push_action( "flon"_n, "setacctnet"_n, mvo()
                           ("account", "flon")
                           ("net_weight", -1)
                        )
   );

   BOOST_REQUIRE_EQUAL( success(),
                        push_action( "flon"_n, "setacctcpu"_n, mvo()
                           ("account", "flon")
                           ("cpu_weight", -1)

                        )
   );

   BOOST_REQUIRE_EQUAL( success(),
                        push_action( "flon"_n, "setalimits"_n, mvo()
                                          ("account", "flon.saving")
                                          ("ram_bytes", ram_bytes_needed)
                                          ("net_weight", -1)
                                          ("cpu_weight", -1)
                        )
   );

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( buy_pin_sell_ram, eosio_system_tester ) try {
   BOOST_REQUIRE( get_total_stake( "flon" ).is_null() );

   transfer( "flon"_n, "alice1111111"_n, core_sym::from_string("1020.0000") );

   auto error_msg = stake( "alice1111111"_n, "flon"_n, core_sym::from_string("10.0000"), core_sym::from_string("10.0000") );
   auto semicolon_pos = error_msg.find(';');

   BOOST_REQUIRE_EQUAL( error("account flon has insufficient ram"),
                        error_msg.substr(0, semicolon_pos) );

   int64_t ram_bytes_needed = 0;
   {
      std::istringstream s( error_msg );
      s.seekg( semicolon_pos + 7, std::ios_base::beg );
      s >> ram_bytes_needed;
      ram_bytes_needed += ram_bytes_needed/10; // enough buffer to make up for buyrambytes estimation errors
   }

   auto alice_original_balance = get_balance( "alice1111111"_n );

   BOOST_REQUIRE_EQUAL( success(), buyrambytes( "alice1111111"_n, "flon"_n, static_cast<uint32_t>(ram_bytes_needed) ) );

   auto tokens_paid_for_ram = alice_original_balance - get_balance( "alice1111111"_n );

   auto total_res = get_total_stake( "flon" );

   REQUIRE_MATCHING_OBJECT( total_res, mvo()
      ("owner", "flon")
      ("net_weight", core_sym::from_string("0.0000"))
      ("cpu_weight", core_sym::from_string("0.0000"))
      ("ram_bytes",  total_res["ram_bytes"].as_int64() )
   );

   BOOST_REQUIRE_EQUAL( wasm_assert_msg( "only supports unlimited accounts" ),
                        push_action( "flon"_n, "setalimits"_n, mvo()
                                          ("account", "flon")
                                          ("ram_bytes", ram_bytes_needed)
                                          ("net_weight", -1)
                                          ("cpu_weight", -1)
                        )
   );


   auto eosio_original_balance = get_balance( "flon"_n );

   BOOST_REQUIRE_EQUAL( success(), sellram( "flon"_n, total_res["ram_bytes"].as_int64() ) );

   auto tokens_received_by_selling_ram = get_balance( "flon"_n ) - eosio_original_balance;

   BOOST_REQUIRE( double(tokens_paid_for_ram.get_amount() - tokens_received_by_selling_ram.get_amount()) / tokens_paid_for_ram.get_amount() < 0.01 );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()

#include <boost/test/unit_test.hpp>
#include <eosio/chain/resource_limits.hpp>
#include "flon.system_tester.hpp"

using namespace eosio_system;

// Current resource accounting uses Gas; the RAM market actions were retired.
BOOST_AUTO_TEST_SUITE(eosio_system_gas_tests)

static uint64_t reserved_gas(eosio_system_tester& test, account_name account) {
   uint64_t gas = 0;
   bool unlimited = false;
   test.control->get_resource_limits_manager().get_account_limits(account, gas, unlimited);
   BOOST_REQUIRE(!unlimited);
   return gas;
}

BOOST_FIXTURE_TEST_CASE(buy_gas_for_self, eosio_system_tester) try {
   transfer("flon"_n, "alice1111111"_n, core_sym::from_string("200.0000"));
   const auto before_gas = reserved_gas(*this, "alice1111111"_n);
   const auto before_balance = get_balance("alice1111111"_n);
   BOOST_REQUIRE_EQUAL(success(), push_action("alice1111111"_n, "buygasself"_n,
      mvo()("account", "alice1111111")("quant", "100.0000 TST")));
   BOOST_REQUIRE_GT(reserved_gas(*this, "alice1111111"_n), before_gas);
   BOOST_REQUIRE_EQUAL(before_balance - core_sym::from_string("100.0000"), get_balance("alice1111111"_n));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(buy_gas_for_other_account, eosio_system_tester) try {
   transfer("flon"_n, "alice1111111"_n, core_sym::from_string("10.0000"));
   const auto bob_before = reserved_gas(*this, "bob111111111"_n);
   BOOST_REQUIRE_EQUAL(success(), push_action("alice1111111"_n, "buygas"_n,
      mvo()("payer", "alice1111111")("receiver", "bob111111111")("quant", "2.0000 TST")));
   BOOST_REQUIRE_EQUAL(bob_before + 20000, reserved_gas(*this, "bob111111111"_n));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(buy_gas_requires_authority, eosio_system_tester) try {
   transfer("flon"_n, "alice1111111"_n, core_sym::from_string("10.0000"));
   BOOST_REQUIRE_NE(success(), push_action("bob111111111"_n, "buygas"_n,
      mvo()("payer", "alice1111111")("receiver", "bob111111111")("quant", "1.0000 TST")));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(buy_gas_rejects_zero_quantity, eosio_system_tester) try {
   transfer("flon"_n, "alice1111111"_n, core_sym::from_string("10.0000"));
   const auto before = reserved_gas(*this, "alice1111111"_n);
   BOOST_REQUIRE_EQUAL(wasm_assert_msg("must purchase a positive amount"),
      push_action("alice1111111"_n, "buygasself"_n,
         mvo()("account", "alice1111111")("quant", "0.0000 TST")));
   BOOST_REQUIRE_EQUAL(before, reserved_gas(*this, "alice1111111"_n));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(buy_gas_rejects_missing_receiver, eosio_system_tester) try {
   transfer("flon"_n, "alice1111111"_n, core_sym::from_string("10.0000"));
   BOOST_REQUIRE_EQUAL(wasm_assert_msg("receiver account not exists"),
      push_action("alice1111111"_n, "buygas"_n,
         mvo()("payer", "alice1111111")("receiver", "missingacct1")("quant", "1.0000 TST")));
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()

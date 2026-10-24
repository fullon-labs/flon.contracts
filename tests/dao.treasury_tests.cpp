#include <boost/test/unit_test.hpp>
#include <eosio/chain/abi_serializer.hpp>
#include <eosio/testing/tester.hpp>

#include "contracts.hpp"

using namespace eosio::chain;
using namespace eosio::testing;
using namespace fc;

namespace {

using mvo = fc::mutable_variant_object;

class dao_treasury_tester : public tester {
 public:
   const symbol flon_symbol{8, "FLON"};

   dao_treasury_tester() {
      produce_blocks(2);
      create_accounts({"flon.token"_n, "dao.treasury"_n, "dao.gov"_n,
                       "alice"_n, "bob"_n});
      produce_blocks(2);

      set_code("flon.token"_n, contracts::token_wasm());
      set_abi("flon.token"_n, contracts::token_abi().data());
      set_code("dao.treasury"_n, contracts::dao_treasury_wasm());
      set_abi("dao.treasury"_n, contracts::dao_treasury_abi().data());
      load_abi("flon.token"_n, token_abi);
      load_abi("dao.treasury"_n, treasury_abi);

      set_authority(
         "dao.treasury"_n, config::active_name,
         authority(1,
                   vector<key_weight>{{get_public_key("dao.treasury"_n, "active"), 1}},
                   vector<permission_level_weight>{{{"dao.treasury"_n,
                                                      config::eosio_code_name}, 1}}),
         config::owner_name,
         {{"dao.treasury"_n, config::owner_name}},
         {get_private_key("dao.treasury"_n, "owner")});

      BOOST_REQUIRE_EQUAL(success(), push("flon.token"_n, token_abi, "flon.token"_n,
         "create"_n, mvo()("issuer", "flon"_n)
                          ("maximum_supply", asset(10'000'000'000'000LL, flon_symbol))));
      BOOST_REQUIRE_EQUAL(success(), push("flon.token"_n, token_abi, "flon"_n,
         "issue"_n, mvo()("to", "flon"_n)
                         ("quantity", asset(1'000'000'000'000LL, flon_symbol))
                         ("memo", "treasury test supply")));
      BOOST_REQUIRE_EQUAL(success(), transfer("flon"_n, "alice"_n,
         asset(100'000'000'000LL, flon_symbol), "fund alice"));
      BOOST_REQUIRE_EQUAL(success(), treasury("dao.treasury"_n, "init"_n,
         mvo()("governance", "dao.gov"_n)));
      BOOST_REQUIRE_EQUAL(success(), treasury("dao.gov"_n, "setasset"_n,
         mvo()("token_contract", "flon.token"_n)("token_symbol", flon_symbol)
              ("enabled", true)));
      BOOST_REQUIRE_EQUAL(success(), transfer("alice"_n, "dao.treasury"_n,
         asset(100'000'000'000LL, flon_symbol), "DAO funding"));
   }

   action_result push(name contract, abi_serializer& serializer, name signer,
                      name action_name, const variant_object& data) {
      action act;
      act.account = contract;
      act.name = action_name;
      act.data = serializer.variant_to_binary(
         serializer.get_action_type(action_name), data,
         abi_serializer::create_yield_function(abi_serializer_max_time));
      return base_tester::push_action(std::move(act), signer.to_uint64_t());
   }

   action_result treasury(name signer, name action_name, const variant_object& data) {
      return push("dao.treasury"_n, treasury_abi, signer, action_name, data);
   }

   action_result transfer(name from, name to, asset quantity, const std::string& memo) {
      return push("flon.token"_n, token_abi, from, "transfer"_n,
                  mvo()("from", from)("to", to)("quantity", quantity)("memo", memo));
   }

   action_result reserve(uint64_t proposal_id, int64_t whole_flon,
                         uint16_t max_payments, uint32_t lifetime_seconds) {
      const auto expires = time_point_sec(control->head_block_time() +
                                          fc::seconds(lifetime_seconds));
      return treasury("dao.gov"_n, "reserve"_n,
         mvo()("proposal_id", proposal_id)("recipient", "bob"_n)
              ("token_contract", "flon.token"_n)
              ("quantity", asset(whole_flon * 100'000'000LL, flon_symbol))
              ("max_payments", max_payments)("expires_at", expires)
              ("memo_hash", std::string(64, '2')));
   }

   fc::variant row(name table, uint64_t primary_key, const char* type,
                   name scope = "dao.treasury"_n) {
      const auto data = get_row_by_account("dao.treasury"_n, scope, table,
                                           name(primary_key));
      BOOST_REQUIRE(!data.empty());
      return treasury_abi.binary_to_variant(
         type, data,
         abi_serializer::create_yield_function(abi_serializer_max_time));
   }

   asset balance(name owner) {
      const auto data = get_row_by_account("flon.token"_n, owner, "accounts"_n,
                                           name(flon_symbol.to_symbol_code().value));
      if (data.empty()) return asset(0, flon_symbol);
      return token_abi.binary_to_variant(
         "account", data,
         abi_serializer::create_yield_function(abi_serializer_max_time))
         ["balance"].as<asset>();
   }

 private:
   void load_abi(name account, abi_serializer& serializer) {
      const auto& object = control->db().get<account_object, by_name>(account);
      abi_def abi;
      BOOST_REQUIRE(abi_serializer::to_abi(object.abi, abi));
      serializer.set_abi(abi, abi_serializer::create_yield_function(abi_serializer_max_time));
   }

 public:
   abi_serializer token_abi;
   abi_serializer treasury_abi;
};

} // namespace

BOOST_AUTO_TEST_SUITE(dao_treasury_tests)

BOOST_FIXTURE_TEST_CASE(reserve_partial_pay_and_complete, dao_treasury_tester) try {
   const auto asset_before = row("assets"_n, 1, "treasury_asset");
   BOOST_REQUIRE_EQUAL(asset_before["liquid"].as<asset>(),
                       asset(100'000'000'000LL, flon_symbol));
   BOOST_REQUIRE_EQUAL(asset_before["reserved"].as<asset>(), asset(0, flon_symbol));

   BOOST_REQUIRE_EQUAL(success(), reserve(101, 600, 2, 600));
   BOOST_REQUIRE_EQUAL(wasm_assert_msg("insufficient unreserved treasury balance"),
                       reserve(102, 500, 1, 600));
   BOOST_REQUIRE_EQUAL(success(), treasury("dao.gov"_n, "pay"_n,
      mvo()("grant_id", 1)("quantity", asset(20'000'000'000LL, flon_symbol))
           ("memo", "milestone 1")));
   BOOST_REQUIRE_EQUAL(balance("bob"_n), asset(20'000'000'000LL, flon_symbol));

   auto treasury_asset = row("assets"_n, 1, "treasury_asset");
   BOOST_REQUIRE_EQUAL(treasury_asset["liquid"].as<asset>(),
                       asset(80'000'000'000LL, flon_symbol));
   BOOST_REQUIRE_EQUAL(treasury_asset["reserved"].as<asset>(),
                       asset(40'000'000'000LL, flon_symbol));
   BOOST_REQUIRE_EQUAL(success(), treasury("dao.gov"_n, "pay"_n,
      mvo()("grant_id", 1)("quantity", asset(40'000'000'000LL, flon_symbol))
           ("memo", "milestone 2")));
   const auto grant = row("grants"_n, 1, "grant");
   BOOST_REQUIRE_EQUAL(grant["status"].as_uint64(), 1u);
   BOOST_REQUIRE_EQUAL(grant["paid"].as<asset>(), asset(60'000'000'000LL, flon_symbol));
   BOOST_REQUIRE_EQUAL(balance("bob"_n), asset(60'000'000'000LL, flon_symbol));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(release_and_permissionless_expiry, dao_treasury_tester) try {
   BOOST_REQUIRE_EQUAL(success(), reserve(201, 300, 3, 600));
   BOOST_REQUIRE_EQUAL(success(), treasury("dao.gov"_n, "release"_n,
      mvo()("grant_id", 1)("reason", "proposal superseded")));
   BOOST_REQUIRE_EQUAL(row("grants"_n, 1, "grant")["status"].as_uint64(), 2u);
   BOOST_REQUIRE_EQUAL(row("assets"_n, 1, "treasury_asset")["reserved"].as<asset>(),
                       asset(0, flon_symbol));

   BOOST_REQUIRE_EQUAL(success(), reserve(202, 100, 1, 60));
   BOOST_REQUIRE_EQUAL(wasm_assert_msg("grant has not expired"),
      treasury("alice"_n, "closegrant"_n, mvo()("grant_id", 2)));
   produce_block(fc::seconds(61));
   BOOST_REQUIRE_EQUAL(success(), treasury("alice"_n, "closegrant"_n,
      mvo()("grant_id", 2)));
   BOOST_REQUIRE_EQUAL(row("grants"_n, 2, "grant")["status"].as_uint64(), 3u);
   BOOST_REQUIRE_EQUAL(row("assets"_n, 1, "treasury_asset")["reserved"].as<asset>(),
                       asset(0, flon_symbol));
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()

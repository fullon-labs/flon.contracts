#include <boost/test/unit_test.hpp>
#include <eosio/chain/abi_serializer.hpp>
#include <eosio/testing/tester.hpp>

#include "flon.system_tester.hpp"

using namespace eosio::chain;
using namespace eosio::testing;
using namespace fc;

namespace {

using mvo = fc::mutable_variant_object;

class gas_reward_tester : public eosio_system::base_system_tester {
 public:
   const symbol core_symbol{4, "FLON"};

   gas_reward_tester() {
      produce_blocks(2);
      create_accounts({"flon.token"_n, "gas.reward"_n,
                       "alice"_n, "bob"_n, "carol"_n, "sink"_n});
      produce_blocks(2);

      set_code("flon.token"_n, contracts::token_wasm());
      set_abi("flon.token"_n, contracts::token_abi().data());
      set_code("gas.reward"_n, contracts::gas_reward_wasm());
      set_abi("gas.reward"_n, contracts::gas_reward_abi().data());

      load_abi("flon.token"_n, token_abi);
      load_abi("gas.reward"_n, reward_abi);

      BOOST_REQUIRE_EQUAL(success(), push("flon.token"_n, token_abi, "flon.token"_n,
         "create"_n, mvo()("issuer", "flon"_n)("maximum_supply", asset(10'000'000, core_symbol))));
      BOOST_REQUIRE_EQUAL(success(), push("flon.token"_n, token_abi, "flon"_n,
         "issue"_n, mvo()("to", "flon"_n)("quantity", asset(1'000'000, core_symbol))
                              ("memo", "test supply")));
      BOOST_REQUIRE_EQUAL(success(), transfer("flon"_n, "flon.gas"_n,
                                              asset(500'000, core_symbol), "fund gas"));
      BOOST_REQUIRE_EQUAL(success(), push("gas.reward"_n, reward_abi, "gas.reward"_n,
         "init"_n, mvo()("core_symbol", core_symbol)));
      produce_block();
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

   action_result reward(name signer, name action_name, const variant_object& data) {
      return push("gas.reward"_n, reward_abi, signer, action_name, data);
   }

   action_result transfer(name from, name to, asset quantity, const std::string& memo) {
      return push("flon.token"_n, token_abi, from, "transfer"_n,
                  mvo()("from", from)("to", to)("quantity", quantity)("memo", memo));
   }

   action_result transfer_from_gas(name to, asset quantity, const std::string& memo) {
      action act;
      act.account = "flon.token"_n;
      act.name = "transfer"_n;
      act.authorization = {{"flon.gas"_n, config::active_name}};
      act.data = token_abi.variant_to_binary(
         token_abi.get_action_type(act.name),
         mvo()("from", "flon.gas"_n)("to", to)("quantity", quantity)("memo", memo),
         abi_serializer::create_yield_function(abi_serializer_max_time));
      signed_transaction trx;
      trx.actions.emplace_back(std::move(act));
      set_transaction_headers(trx);
      // flon.gas@active delegates to flon@active in genesis.
      trx.sign(get_private_key("flon"_n, "active"), control->get_chain_id());
      try {
         push_transaction(trx);
      } catch (const fc::exception& ex) {
         return error(ex.top_message());
      }
      produce_block();
      return success();
   }

   fc::variant get_reward_row(name scope, name table, uint64_t primary_key,
                              const char* type) {
      const auto data = get_row_by_account("gas.reward"_n, scope, table, name(primary_key));
      return data.empty() ? fc::variant() : reward_abi.binary_to_variant(
         type, data, abi_serializer::create_yield_function(abi_serializer_max_time));
   }

   fc::variant get_epoch(uint64_t epoch_id) {
      return get_reward_row("gas.reward"_n, "rewardepochs"_n, epoch_id, "reward_epoch");
   }

   fc::variant get_node(name owner) {
      return get_reward_row("gas.reward"_n, "noderewards"_n, owner.to_uint64_t(), "node_reward");
   }

   fc::variant get_global() {
      return get_reward_row("gas.reward"_n, "global"_n, "global"_n.to_uint64_t(), "global_state");
   }

   asset balance(name owner) {
      const auto data = get_row_by_account("flon.token"_n, owner, "accounts"_n,
                                           name(core_symbol.to_symbol_code().value));
      if (data.empty()) return asset(0, core_symbol);
      return token_abi.binary_to_variant(
         "account", data, abi_serializer::create_yield_function(abi_serializer_max_time))
         ["balance"].as<asset>();
   }

   void open_and_fund(uint64_t epoch_id, asset quantity) {
      BOOST_REQUIRE_EQUAL(success(), reward("flon"_n, "openepoch"_n,
         mvo()("epoch_id", epoch_id)("expected", quantity)));
      BOOST_REQUIRE_EQUAL(success(), transfer_from_gas(
         "gas.reward"_n, quantity, "gas-reward:" + std::to_string(epoch_id)));
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
   abi_serializer reward_abi;
};

} // namespace

BOOST_AUTO_TEST_SUITE(gas_reward_tests)

BOOST_FIXTURE_TEST_CASE(cross_batch_allocation_preserves_remainder, gas_reward_tester) try {
   const uint64_t epoch = 7;
   open_and_fund(epoch, asset(10'000, core_symbol));

   BOOST_REQUIRE_EQUAL(success(), reward("flon"_n, "creditbatch"_n,
      mvo()("epoch_id", epoch)
           ("owners", std::vector<name>{"alice"_n, "bob"_n})
           ("quantities", std::vector<asset>{asset(3'333, core_symbol),
                                               asset(3'333, core_symbol)})));
   BOOST_REQUIRE_EQUAL(success(), reward("flon"_n, "creditbatch"_n,
      mvo()("epoch_id", epoch)
           ("owners", std::vector<name>{"carol"_n})
           ("quantities", std::vector<asset>{asset(3'334, core_symbol)})));

   const auto epoch_row = get_epoch(epoch);
   BOOST_REQUIRE_EQUAL(epoch_row["allocated"].as<asset>(), asset(10'000, core_symbol));
   BOOST_REQUIRE_EQUAL(epoch_row["credited"].as_uint64(), 3u);
   BOOST_REQUIRE_EQUAL(epoch_row["last_credited"].as<name>(), "carol"_n);
   BOOST_REQUIRE_EQUAL(get_node("alice"_n)["unclaimed"].as<asset>(), asset(3'333, core_symbol));
   BOOST_REQUIRE_EQUAL(get_node("bob"_n)["unclaimed"].as<asset>(), asset(3'333, core_symbol));
   BOOST_REQUIRE_EQUAL(get_node("carol"_n)["unclaimed"].as<asset>(), asset(3'334, core_symbol));

   BOOST_REQUIRE_EQUAL(success(), reward("flon"_n, "finalize"_n,
      mvo()("epoch_id", epoch)("allocation_hash", epoch_row["allocation_hash"])));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(duplicate_credit_rolls_back, gas_reward_tester) try {
   const uint64_t epoch = 8;
   open_and_fund(epoch, asset(1'000, core_symbol));
   BOOST_REQUIRE_EQUAL(success(), reward("flon"_n, "creditbatch"_n,
      mvo()("epoch_id", epoch)("owners", std::vector<name>{"alice"_n})
           ("quantities", std::vector<asset>{asset(400, core_symbol)})));

   const auto before_epoch = get_epoch(epoch);
   const auto before_node = get_node("alice"_n);
   BOOST_REQUIRE_EQUAL(wasm_assert_msg("owners must be globally sorted and unique"),
      reward("flon"_n, "creditbatch"_n,
         mvo()("epoch_id", epoch)("owners", std::vector<name>{"alice"_n})
              ("quantities", std::vector<asset>{asset(100, core_symbol)})));
   BOOST_REQUIRE_EQUAL(get_epoch(epoch)["allocated"].as<asset>(),
                       before_epoch["allocated"].as<asset>());
   BOOST_REQUIRE_EQUAL(get_node("alice"_n)["unclaimed"].as<asset>(),
                       before_node["unclaimed"].as<asset>());
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(overfunding_transfer_rolls_back, gas_reward_tester) try {
   const uint64_t epoch = 9;
   BOOST_REQUIRE_EQUAL(success(), reward("flon"_n, "openepoch"_n,
      mvo()("epoch_id", epoch)("expected", asset(1'000, core_symbol))));
   const auto gas_before = balance("flon.gas"_n);
   const auto reward_before = balance("gas.reward"_n);

   BOOST_REQUIRE_EQUAL(wasm_assert_msg("epoch overfunded"),
      transfer_from_gas("gas.reward"_n, asset(1'001, core_symbol),
                        "gas-reward:" + std::to_string(epoch)));
   BOOST_REQUIRE_EQUAL(balance("flon.gas"_n), gas_before);
   BOOST_REQUIRE_EQUAL(balance("gas.reward"_n), reward_before);
   BOOST_REQUIRE_EQUAL(get_epoch(epoch)["funded"].as<asset>(), asset(0, core_symbol));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(claim_transfer_failure_rolls_back_ledger, gas_reward_tester) try {
   const uint64_t epoch = 10;
   open_and_fund(epoch, asset(1'000, core_symbol));
   BOOST_REQUIRE_EQUAL(success(), reward("flon"_n, "creditbatch"_n,
      mvo()("epoch_id", epoch)("owners", std::vector<name>{"alice"_n})
           ("quantities", std::vector<asset>{asset(1'000, core_symbol)})));

   // Simulate an insolvent reward account. claimgas updates its ledger before
   // issuing the inline transfer, so the failed transfer must roll everything back.
   BOOST_REQUIRE_EQUAL(success(), transfer("gas.reward"_n, "sink"_n,
                                           asset(1'000, core_symbol), "drain for rollback test"));
   BOOST_REQUIRE_EQUAL(asset(0, core_symbol), balance("gas.reward"_n));
   const auto alice_before = balance("alice"_n);

   BOOST_REQUIRE_EQUAL(wasm_assert_msg("overdrawn balance"),
      reward("alice"_n, "claimgas"_n, mvo()("owner", "alice"_n)));
   const auto node = get_node("alice"_n);
   BOOST_REQUIRE_EQUAL(node["claimed"].as<asset>(), asset(0, core_symbol));
   BOOST_REQUIRE_EQUAL(node["unclaimed"].as<asset>(), asset(1'000, core_symbol));
   BOOST_REQUIRE_EQUAL(get_global()["total_claimed"].as<asset>(), asset(0, core_symbol));
   BOOST_REQUIRE_EQUAL(balance("alice"_n), alice_before);
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()

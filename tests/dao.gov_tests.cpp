#include <boost/test/unit_test.hpp>
#include <eosio/chain/abi_serializer.hpp>
#include <eosio/testing/tester.hpp>

#include "contracts.hpp"

using namespace eosio::chain;
using namespace eosio::testing;
using namespace fc;

namespace {

using mvo = fc::mutable_variant_object;

class dao_gov_tester : public tester {
 public:
   const symbol glon_symbol{8, "GLON"};

   dao_gov_tester() {
      produce_blocks(2);
      create_accounts({"dao.gov"_n, "dao.admin"_n, "powersource"_n,
                       "alice"_n, "bob"_n, "carol"_n});
      produce_blocks(2);

      set_code("dao.gov"_n, contracts::dao_gov_wasm());
      set_abi("dao.gov"_n, contracts::dao_gov_abi().data());
      load_abi();
      set_authority(
         "dao.gov"_n, config::active_name,
         authority(1,
                   vector<key_weight>{{get_public_key("dao.gov"_n, "active"), 1}},
                   vector<permission_level_weight>{{{"dao.gov"_n, config::eosio_code_name}, 1}}),
         config::owner_name,
         {{"dao.gov"_n, config::owner_name}},
         {get_private_key("dao.gov"_n, "owner")});

      BOOST_REQUIRE_EQUAL(success(), gov("dao.gov"_n, "init"_n,
         mvo()("admin", "dao.admin"_n)
              ("power_source", "powersource"_n)
              ("governance_symbol", glon_symbol)));
      BOOST_REQUIRE_EQUAL(success(), gov("dao.admin"_n, "setconfig"_n,
         mvo()("proposal_threshold", asset(500'000'000, glon_symbol))
              ("endorsement_threshold", asset(100'000'000, glon_symbol))
              ("required_endorsements", 1)
              ("endorsement_seconds", 60)
              ("voting_delay_seconds", 10)
              ("voting_seconds", 60)
              ("quorum_bps", 4000)
              ("approval_bps", 6000)
              ("timelock_seconds", 60)
              ("execution_grace_seconds", 120)
              ("max_actions", 5)
              ("max_action_bytes", 2048)
              ("max_total_action_bytes", 4096)));
      set_risk(1, 10, 1000, 5001, 100, 5);
      set_risk(2, 20, 1500, 6000, 800, 10);
      set_risk(3, 30, 2500, 6700, 1500, 20);
      set_risk(4, 100, 4000, 7500, 3000, 50);
      set_power("alice"_n, 100);
      set_power("bob"_n, 60);
      set_power("carol"_n, 40);
   }

   action_result gov(name signer, name action_name, const variant_object& data) {
      action act;
      act.account = "dao.gov"_n;
      act.name = action_name;
      act.data = gov_abi.variant_to_binary(
         gov_abi.get_action_type(action_name), data,
         abi_serializer::create_yield_function(abi_serializer_max_time));
      return base_tester::push_action(std::move(act), signer.to_uint64_t());
   }

   action_result set_power(name voter, int64_t whole_glon) {
      return gov("powersource"_n, "setpower"_n,
                 mvo()("voter", voter)
                      ("power", asset(whole_glon * 100'000'000, glon_symbol)));
   }

   action_result set_risk(uint8_t level, int64_t min_total,
                          uint16_t quorum, uint16_t approval,
                          uint16_t min_support, int64_t proposer) {
      return gov("flon"_n, "setriskcfg"_n,
         mvo()("risk_level", level)
              ("min_total_power", asset(min_total * 100'000'000, glon_symbol))
              ("quorum_bps", quorum)
              ("approval_bps", approval)
              ("min_support_bps", min_support)
              ("proposal_threshold", asset(proposer * 100'000'000, glon_symbol))
              ("endorsement_threshold", asset(100'000'000, glon_symbol))
              ("min_endorsement_bps", 0)
              ("required_endorsements", 1)
              ("endorsement_seconds", 60)
              ("voting_delay_seconds", 10)
              ("voting_seconds", 60)
              ("timelock_seconds", 60)
              ("execution_grace_seconds", 120)
              ("enabled", true));
   }

   action_result set_action_risk(name contract, name action_name, uint8_t level) {
      return gov("flon"_n, "setactrisk"_n,
         mvo()("contract", contract)("action_name", action_name)
              ("risk_level", level)("enabled", true));
   }

   variant_object config_values(int64_t proposal_threshold = 500'000'000) {
      return mvo()("proposal_threshold", asset(proposal_threshold, glon_symbol))
                  ("endorsement_threshold", asset(100'000'000, glon_symbol))
                  ("required_endorsements", 1)
                  ("endorsement_seconds", 60)
                  ("voting_delay_seconds", 10)
                  ("voting_seconds", 60)
                  ("quorum_bps", 4000)
                  ("approval_bps", 6000)
                  ("timelock_seconds", 60)
                  ("execution_grace_seconds", 120)
                  ("max_actions", 5)
                  ("max_action_bytes", 2048)
                  ("max_total_action_bytes", 4096);
   }

   std::vector<char> config_payload(int64_t proposal_threshold) {
      return gov_abi.variant_to_binary(
         gov_abi.get_action_type("setconfig"_n), config_values(proposal_threshold),
         abi_serializer::create_yield_function(abi_serializer_max_time));
   }

   action_result propose(name proposer = "alice"_n, const variants& actions = {}) {
      return gov(proposer, "propose"_n,
         mvo()("proposer", proposer)
              ("proposal_type", "text"_n)
              ("title", "Adopt governance policy")
              ("summary", "A deterministic proposal used by the contract test suite.")
              ("content_hash", std::string(64, '1'))
              ("metadata_uri", "ipfs://bafy-governance-metadata")
              ("discussion_uri", "https://forum.flon.network/t/1")
              ("actions", actions));
   }

   void pass_proposal(uint64_t proposal_id = 1) {
      BOOST_REQUIRE_EQUAL(success(), gov("bob"_n, "endorse"_n,
         mvo()("endorser", "bob"_n)("proposal_id", proposal_id)));
      produce_block(fc::seconds(61));
      BOOST_REQUIRE_EQUAL(success(), gov("carol"_n, "activate"_n,
         mvo()("proposal_id", proposal_id)));
      produce_block(fc::seconds(11));
      BOOST_REQUIRE_EQUAL(success(), gov("alice"_n, "castvote"_n,
         mvo()("voter", "alice"_n)("proposal_id", proposal_id)("choice", 1)));
      produce_block(fc::seconds(61));
      BOOST_REQUIRE_EQUAL(success(), gov("bob"_n, "finalize"_n,
         mvo()("proposal_id", proposal_id)));
   }

   fc::variant proposal(uint64_t proposal_id) {
      const auto data = get_row_by_account("dao.gov"_n, "dao.gov"_n,
                                           "proposals"_n, name(proposal_id));
      BOOST_REQUIRE(!data.empty());
      return gov_abi.binary_to_variant(
         "proposal", data,
         abi_serializer::create_yield_function(abi_serializer_max_time));
   }

   fc::variant governance_config() {
      const auto data = get_row_by_account("dao.gov"_n, "dao.gov"_n,
                                           "config"_n, "config"_n);
      BOOST_REQUIRE(!data.empty());
      return gov_abi.binary_to_variant(
         "governance_config", data,
         abi_serializer::create_yield_function(abi_serializer_max_time));
   }

 private:
   void load_abi() {
      const auto& object = control->db().get<account_object, by_name>("dao.gov"_n);
      abi_def abi;
      BOOST_REQUIRE(abi_serializer::to_abi(object.abi, abi));
      gov_abi.set_abi(abi, abi_serializer::create_yield_function(abi_serializer_max_time));
   }

 public:
   abi_serializer gov_abi;
};

} // namespace

BOOST_AUTO_TEST_SUITE(dao_gov_tests)

BOOST_FIXTURE_TEST_CASE(snapshot_vote_and_finalize, dao_gov_tester) try {
   BOOST_REQUIRE_EQUAL(success(), propose());
   BOOST_REQUIRE_EQUAL(success(), gov("bob"_n, "endorse"_n,
      mvo()("endorser", "bob"_n)("proposal_id", 1)));

   produce_block(fc::seconds(61));
   BOOST_REQUIRE_EQUAL(success(), gov("carol"_n, "activate"_n,
      mvo()("proposal_id", 1)));

   const auto activated = proposal(1);
   BOOST_REQUIRE_EQUAL(activated["snapshot_sequence"].as_uint64(), 3u);
   BOOST_REQUIRE_EQUAL(activated["snapshot_total_power"].as<asset>(),
                       asset(200 * 100'000'000LL, glon_symbol));
   BOOST_REQUIRE_EQUAL(activated["snapshot_quorum_bps"].as_uint64(), 1000u);
   BOOST_REQUIRE_EQUAL(activated["snapshot_approval_bps"].as_uint64(), 5001u);

   // Governance rules are frozen at activation and cannot be changed retroactively.
   BOOST_REQUIRE_EQUAL(success(), gov("dao.admin"_n, "setconfig"_n,
      mvo()("proposal_threshold", asset(500'000'000, glon_symbol))
           ("endorsement_threshold", asset(100'000'000, glon_symbol))
           ("required_endorsements", 1)("endorsement_seconds", 60)
           ("voting_delay_seconds", 10)("voting_seconds", 60)
           ("quorum_bps", 10000)("approval_bps", 10000)
           ("timelock_seconds", 300)("execution_grace_seconds", 300)
           ("max_actions", 5)("max_action_bytes", 2048)
           ("max_total_action_bytes", 4096)));

   // Later GLON changes must not rewrite the proposal snapshot.
   BOOST_REQUIRE_EQUAL(success(), set_power("bob"_n, 0));
   produce_block(fc::seconds(11));
   BOOST_REQUIRE_EQUAL(success(), gov("bob"_n, "castvote"_n,
      mvo()("voter", "bob"_n)("proposal_id", 1)("choice", 1)));
   BOOST_REQUIRE_EQUAL(success(), gov("carol"_n, "castvote"_n,
      mvo()("voter", "carol"_n)("proposal_id", 1)("choice", 0)));
   BOOST_REQUIRE_EQUAL(wasm_assert_msg("account already voted"), gov("bob"_n, "castvote"_n,
      mvo()("voter", "bob"_n)("proposal_id", 1)("choice", 1)));

   produce_block(fc::seconds(61));
   BOOST_REQUIRE_EQUAL(success(), gov("alice"_n, "finalize"_n,
      mvo()("proposal_id", 1)));

   const auto finalized = proposal(1);
   BOOST_REQUIRE_EQUAL(finalized["status"].as_uint64(), 4u);
   BOOST_REQUIRE_EQUAL(finalized["for_votes"].as<asset>(),
                       asset(60 * 100'000'000LL, glon_symbol));
   BOOST_REQUIRE_EQUAL(finalized["against_votes"].as<asset>(),
                       asset(40 * 100'000'000LL, glon_symbol));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(thresholds_duplicate_endorsement_and_cancel, dao_gov_tester) try {
   BOOST_REQUIRE_EQUAL(success(), set_power("carol"_n, 4));
   BOOST_REQUIRE_EQUAL(wasm_assert_msg("insufficient governance power to propose"),
                       propose("carol"_n));

   BOOST_REQUIRE_EQUAL(success(), propose());
   BOOST_REQUIRE_EQUAL(success(), gov("bob"_n, "endorse"_n,
      mvo()("endorser", "bob"_n)("proposal_id", 1)));
   BOOST_REQUIRE_EQUAL(wasm_assert_msg("proposal already endorsed by account"),
      gov("bob"_n, "endorse"_n,
          mvo()("endorser", "bob"_n)("proposal_id", 1)));
   BOOST_REQUIRE_EQUAL(wasm_assert_msg("only proposer may cancel proposal"),
      gov("bob"_n, "cancel"_n,
          mvo()("proposer", "bob"_n)("proposal_id", 1)));
   BOOST_REQUIRE_EQUAL(success(), gov("alice"_n, "cancel"_n,
      mvo()("proposer", "alice"_n)("proposal_id", 1)));
   BOOST_REQUIRE_EQUAL(proposal(1)["status"].as_uint64(), 5u);
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(permissionless_timing_and_quorum_defeat, dao_gov_tester) try {
   BOOST_REQUIRE_EQUAL(success(), propose());
   BOOST_REQUIRE_EQUAL(success(), gov("bob"_n, "endorse"_n,
      mvo()("endorser", "bob"_n)("proposal_id", 1)));
   BOOST_REQUIRE_EQUAL(wasm_assert_msg("endorsement period is still active"),
      gov("carol"_n, "activate"_n, mvo()("proposal_id", 1)));

   produce_block(fc::seconds(61));
   BOOST_REQUIRE_EQUAL(success(), gov("carol"_n, "activate"_n,
      mvo()("proposal_id", 1)));
   BOOST_REQUIRE_EQUAL(wasm_assert_msg("proposal is not open for voting"),
      gov("carol"_n, "castvote"_n,
          mvo()("voter", "carol"_n)("proposal_id", 1)("choice", 0)));

   produce_block(fc::seconds(11));
   BOOST_REQUIRE_EQUAL(success(), gov("carol"_n, "castvote"_n,
      mvo()("voter", "carol"_n)("proposal_id", 1)("choice", 0)));
   produce_block(fc::seconds(61));
   BOOST_REQUIRE_EQUAL(success(), gov("bob"_n, "finalize"_n,
      mvo()("proposal_id", 1)));
   BOOST_REQUIRE_EQUAL(proposal(1)["status"].as_uint64(), 3u);
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(timelock_allowlist_and_permissionless_execute, dao_gov_tester) try {
   BOOST_REQUIRE_EQUAL(wasm_assert_msg(
      "authorization actor must be dao.gov or the target contract"),
      gov("dao.admin"_n, "setallow"_n,
         mvo()("contract", "dao.gov"_n)("action_name", "setconfig"_n)
              ("authorization_actor", "alice"_n)
              ("authorization_permission", "active"_n)
              ("max_data_bytes", 2048)("enabled", true)));
   BOOST_REQUIRE_EQUAL(success(), gov("dao.admin"_n, "setallow"_n,
      mvo()("contract", "dao.gov"_n)("action_name", "setconfig"_n)
           ("authorization_actor", "dao.gov"_n)
           ("authorization_permission", "active"_n)
           ("max_data_bytes", 2048)("enabled", true)));
   BOOST_REQUIRE_EQUAL(success(), set_action_risk("dao.gov"_n, "setconfig"_n, 4));
   BOOST_REQUIRE_EQUAL(wasm_assert_msg("allowlist authorization is immutable"),
      gov("dao.admin"_n, "setallow"_n,
         mvo()("contract", "dao.gov"_n)("action_name", "setconfig"_n)
              ("authorization_actor", "dao.gov"_n)
              ("authorization_permission", "owner"_n)
              ("max_data_bytes", 2048)("enabled", true)));
   BOOST_REQUIRE_EQUAL(wasm_assert_msg(
      "target contract owner or active permission is forbidden"),
      gov("dao.admin"_n, "setallow"_n,
         mvo()("contract", "alice"_n)("action_name", "test"_n)
              ("authorization_actor", "alice"_n)
              ("authorization_permission", "active"_n)
              ("max_data_bytes", 16)("enabled", true)));
   variants actions{mvo()("contract", "dao.gov"_n)
                        ("action_name", "setconfig"_n)
                        ("data", config_payload(600'000'000))};
   BOOST_REQUIRE_EQUAL(success(), propose("alice"_n, actions));
   pass_proposal();
   BOOST_REQUIRE_EQUAL(success(), gov("carol"_n, "queue"_n,
      mvo()("proposal_id", 1)));
   BOOST_REQUIRE_EQUAL(proposal(1)["status"].as_uint64(), 6u);
   BOOST_REQUIRE_EQUAL(wasm_assert_msg("timelock has not expired"),
      gov("bob"_n, "execute"_n, mvo()("proposal_id", 1)));

   BOOST_REQUIRE_EQUAL(success(), gov("dao.admin"_n, "setallow"_n,
      mvo()("contract", "dao.gov"_n)("action_name", "setconfig"_n)
           ("authorization_actor", "dao.gov"_n)
           ("authorization_permission", "active"_n)
           ("max_data_bytes", 2048)("enabled", false)));
   produce_block(fc::seconds(61));
   BOOST_REQUIRE_EQUAL(wasm_assert_msg("proposal action is not allowlisted"),
      gov("carol"_n, "execute"_n, mvo()("proposal_id", 1)));
   BOOST_REQUIRE_EQUAL(success(), gov("dao.admin"_n, "setallow"_n,
      mvo()("contract", "dao.gov"_n)("action_name", "setconfig"_n)
           ("authorization_actor", "dao.gov"_n)
           ("authorization_permission", "active"_n)
           ("max_data_bytes", 2048)("enabled", true)));
   BOOST_REQUIRE_EQUAL(success(), gov("carol"_n, "execute"_n,
      mvo()("proposal_id", 1)));
   BOOST_REQUIRE_EQUAL(proposal(1)["status"].as_uint64(), 7u);
   BOOST_REQUIRE_EQUAL(governance_config()["proposal_threshold"].as<asset>(),
                       asset(600'000'000, glon_symbol));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(expired_execution_window, dao_gov_tester) try {
   BOOST_REQUIRE_EQUAL(success(), gov("dao.admin"_n, "setallow"_n,
      mvo()("contract", "dao.gov"_n)("action_name", "setconfig"_n)
           ("authorization_actor", "dao.gov"_n)
           ("authorization_permission", "active"_n)
           ("max_data_bytes", 2048)("enabled", true)));
   BOOST_REQUIRE_EQUAL(success(), set_action_risk("dao.gov"_n, "setconfig"_n, 4));
   variants actions{mvo()("contract", "dao.gov"_n)
                        ("action_name", "setconfig"_n)
                        ("data", config_payload(700'000'000))};
   BOOST_REQUIRE_EQUAL(success(), propose("alice"_n, actions));
   pass_proposal();
   BOOST_REQUIRE_EQUAL(success(), gov("bob"_n, "queue"_n,
      mvo()("proposal_id", 1)));
   produce_block(fc::seconds(181));
   BOOST_REQUIRE_EQUAL(wasm_assert_msg("proposal execution window has expired"),
      gov("alice"_n, "execute"_n, mvo()("proposal_id", 1)));
   BOOST_REQUIRE_EQUAL(success(), gov("alice"_n, "expire"_n,
      mvo()("proposal_id", 1)));
   BOOST_REQUIRE_EQUAL(proposal(1)["status"].as_uint64(), 8u);
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(risk_policy_requires_flon_and_rejects_unclassified_actions,
                        dao_gov_tester) try {
   BOOST_REQUIRE_EQUAL(error("missing authority of flon"),
      gov("dao.admin"_n, "setriskcfg"_n,
         mvo()("risk_level", 1)
              ("min_total_power", asset(10 * 100'000'000LL, glon_symbol))
              ("quorum_bps", 4000)("approval_bps", 6000)
              ("min_support_bps", 3000)
              ("proposal_threshold", asset(5 * 100'000'000LL, glon_symbol))
              ("endorsement_threshold", asset(100'000'000, glon_symbol))
              ("min_endorsement_bps", 0)("required_endorsements", 1)
              ("endorsement_seconds", 60)("voting_delay_seconds", 10)
              ("voting_seconds", 60)("timelock_seconds", 60)
              ("execution_grace_seconds", 120)("enabled", true)));

   BOOST_REQUIRE_EQUAL(success(), gov("dao.admin"_n, "setallow"_n,
      mvo()("contract", "dao.gov"_n)("action_name", "setconfig"_n)
           ("authorization_actor", "dao.gov"_n)
           ("authorization_permission", "active"_n)
           ("max_data_bytes", 2048)("enabled", true)));
   variants actions{mvo()("contract", "dao.gov"_n)
                        ("action_name", "setconfig"_n)
                        ("data", config_payload(600'000'000))};
   BOOST_REQUIRE_EQUAL(wasm_assert_msg("proposal action has no enabled risk policy"),
                       propose("alice"_n, actions));
   BOOST_REQUIRE_EQUAL(success(), set_action_risk("dao.gov"_n, "setconfig"_n, 4));
   BOOST_REQUIRE_EQUAL(success(), propose("alice"_n, actions));

   BOOST_REQUIRE_EQUAL(wasm_assert_msg("governance safety policy cannot be allowlisted"),
      gov("dao.admin"_n, "setallow"_n,
         mvo()("contract", "dao.gov"_n)("action_name", "setriskcfg"_n)
              ("authorization_actor", "dao.gov"_n)
              ("authorization_permission", "active"_n)
              ("max_data_bytes", 512)("enabled", true)));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(l4_requires_network_floor_and_absolute_support,
                        dao_gov_tester) try {
   BOOST_REQUIRE_EQUAL(success(), gov("dao.admin"_n, "setallow"_n,
      mvo()("contract", "dao.gov"_n)("action_name", "setconfig"_n)
           ("authorization_actor", "dao.gov"_n)
           ("authorization_permission", "active"_n)
           ("max_data_bytes", 2048)("enabled", true)));
   BOOST_REQUIRE_EQUAL(success(), set_action_risk("dao.gov"_n, "setconfig"_n, 4));
   variants actions{mvo()("contract", "dao.gov"_n)
                        ("action_name", "setconfig"_n)
                        ("data", config_payload(600'000'000))};
   BOOST_REQUIRE_EQUAL(success(), propose("alice"_n, actions));
   BOOST_REQUIRE_EQUAL(success(), gov("bob"_n, "endorse"_n,
      mvo()("endorser", "bob"_n)("proposal_id", 1)));
   BOOST_REQUIRE_EQUAL(success(), set_power("bob"_n, 0));
   BOOST_REQUIRE_EQUAL(success(), set_power("carol"_n, 0));
   BOOST_REQUIRE_EQUAL(success(), set_power("alice"_n, 99));
   produce_block(fc::seconds(61));
   BOOST_REQUIRE_EQUAL(wasm_assert_msg(
      "insufficient network governance power for this risk level"),
      gov("alice"_n, "activate"_n, mvo()("proposal_id", 1)));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(l4_absolute_support_defeats_minority_approval,
                        dao_gov_tester) try {
   BOOST_REQUIRE_EQUAL(success(), set_risk(4, 100, 2500, 7500, 4000, 50));
   BOOST_REQUIRE_EQUAL(success(), gov("dao.admin"_n, "setallow"_n,
      mvo()("contract", "dao.gov"_n)("action_name", "setconfig"_n)
           ("authorization_actor", "dao.gov"_n)
           ("authorization_permission", "active"_n)
           ("max_data_bytes", 2048)("enabled", true)));
   BOOST_REQUIRE_EQUAL(success(), set_action_risk("dao.gov"_n, "setconfig"_n, 4));
   variants actions{mvo()("contract", "dao.gov"_n)
                        ("action_name", "setconfig"_n)
                        ("data", config_payload(600'000'000))};
   BOOST_REQUIRE_EQUAL(success(), propose("alice"_n, actions));
   BOOST_REQUIRE_EQUAL(success(), gov("carol"_n, "endorse"_n,
      mvo()("endorser", "carol"_n)("proposal_id", 1)));
   produce_block(fc::seconds(61));
   BOOST_REQUIRE_EQUAL(success(), gov("alice"_n, "activate"_n,
      mvo()("proposal_id", 1)));
   produce_block(fc::seconds(11));
   BOOST_REQUIRE_EQUAL(success(), gov("bob"_n, "castvote"_n,
      mvo()("voter", "bob"_n)("proposal_id", 1)("choice", 1)));
   produce_block(fc::seconds(61));
   BOOST_REQUIRE_EQUAL(success(), gov("alice"_n, "finalize"_n,
      mvo()("proposal_id", 1)));
   BOOST_REQUIRE_EQUAL(proposal(1)["status"].as_uint64(), 3u);
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()

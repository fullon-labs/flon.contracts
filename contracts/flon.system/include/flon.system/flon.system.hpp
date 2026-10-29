#pragma once

#include <eosio/asset.hpp>
#include <eosio/binary_extension.hpp>
#include <eosio/privileged.hpp>
#include <eosio/producer_schedule.hpp>
#include <eosio/singleton.hpp>
#include <eosio/system.hpp>
#include <eosio/time.hpp>
#include <eosio/instant_finality.hpp>

#include <flon.system/native.hpp>

#include <deque>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_set>

#define PP(prop) "," #prop ":", prop
#define PP0(prop) #prop ":", prop
#define PRINT_PROPERTIES(...) eosio::print("{", __VA_ARGS__, "}")

#define CHECK(exp, msg) { if (!(exp)) eosio::check(false, msg); }
#define CHECKC(exp, code, msg) \
   { if (!(exp)) eosio::check(false, string("[[") + to_string((int)code) + string("]] ")  \
                                    + string("[[") + _self.to_string() + string("]] ") + msg); }

#ifndef ASSERT
    #define ASSERT(exp) CHECK(exp, #exp)
#endif
#define ENABLE_VOTING_PRODUCER

namespace eosiosystem {

   using std::string;
   using std::optional;
   using eosio::asset;
   using eosio::binary_extension;
   using eosio::block_timestamp;
   using eosio::check;
   using eosio::checksum256;
   using eosio::const_mem_fun;
   using eosio::datastream;
   using eosio::indexed_by;
   using eosio::name;
   using eosio::same_payer;
   using eosio::symbol;
   using eosio::symbol_code;
   using eosio::time_point;
   using eosio::time_point_sec;
   using eosio::unsigned_int;
   using eosio::block_signing_authority;

   enum class err: uint8_t {
      NONE                 = 0,
      RECORD_NOT_FOUND     = 1,
      RECORD_EXISTING      = 2,
      SYMBOL_MISMATCH      = 4,
      PARAM_ERROR          = 5,
      MEMO_FORMAT_ERROR    = 6,
      PAUSED               = 7,
      NO_AUTH              = 8,
      NOT_POSITIVE         = 9,
      NOT_STARTED          = 10,
      OVERSIZED            = 11,
      TIME_EXPIRED         = 12,
      NOTIFY_UNRELATED     = 13,
      ACTION_REDUNDANT     = 14,
      ACCOUNT_INVALID      = 15,
      FEE_INSUFFICIENT     = 16,
      FIRST_CREATOR        = 17,
      STATUS_ERROR         = 18,
      SCORE_NOT_ENOUGH     = 19,
      NEED_REQUIRED_CHECK  = 20,
      VOTE_ERROR           = 100,
      VOTE_REFUND_ERROR    = 101,
      VOTE_CHANGE_ERROR    = 102
   };

   template<typename E, typename F>
   static inline auto has_field( F flags, E field )
   -> std::enable_if_t< std::is_integral_v<F> && std::is_unsigned_v<F> &&
                        std::is_enum_v<E> && std::is_same_v< F, std::underlying_type_t<E> >, bool>
   {
      return ( (flags & static_cast<F>(field)) != 0 );
   }

   template<typename E, typename F>
   static inline auto set_field( F flags, E field, bool value = true )
   -> std::enable_if_t< std::is_integral_v<F> && std::is_unsigned_v<F> &&
                        std::is_enum_v<E> && std::is_same_v< F, std::underlying_type_t<E> >, F >
   {
      if( value )
         return ( flags | static_cast<F>(field) );
      else
         return ( flags & ~static_cast<F>(field) );
   }

   static constexpr uint32_t seconds_per_year      = 365 * 24 * 3600;
   static constexpr uint32_t seconds_per_day       = 24 * 3600;
   static constexpr uint32_t seconds_per_hour      = 3600;
   static constexpr int64_t  useconds_per_year     = int64_t(seconds_per_year) * 1000'000ll;
   static constexpr int64_t  useconds_per_day      = int64_t(seconds_per_day) * 1000'000ll;
   static constexpr int64_t  useconds_per_hour     = int64_t(seconds_per_hour) * 1000'000ll;
   static constexpr uint32_t blocks_per_day        = 2 * seconds_per_day; // half seconds per day

   static constexpr int64_t  min_activated_stake   = 150'000'000'0000;
   static constexpr int64_t  ram_gift_bytes        = 1400;
   static constexpr int64_t  min_pervote_daily_pay = 100'0000;
   static constexpr uint32_t refund_delay_sec      = 3 * 60;

   static constexpr uint32_t ratio_boost           = 10000;

   static constexpr int64_t  reward_halving_period_seconds  = 5 * seconds_per_year;
   static constexpr int64_t  reward_halving_period_blocks   = reward_halving_period_seconds * 1000 / block_timestamp::block_interval_ms;

#ifdef SYSTEM_BLOCKCHAIN_PARAMETERS
   struct blockchain_parameters_v1 : eosio::blockchain_parameters
   {
      eosio::binary_extension<uint32_t> max_action_return_value_size;
      EOSLIB_SERIALIZE_DERIVED( blockchain_parameters_v1, eosio::blockchain_parameters,
                                (max_action_return_value_size) )
   };
   using blockchain_parameters_t = blockchain_parameters_v1;
#else
   using blockchain_parameters_t = eosio::blockchain_parameters;
#endif

#ifdef ENABLE_VOTING_PRODUCER
   static constexpr uint32_t max_vote_producer_count     = 30;
   static constexpr uint32_t vote_interval_sec           = 1 * seconds_per_day;
#endif

  /**
   * The `flon.system` smart contract is provided by `block.one` as a sample system contract, and it defines the structures and actions needed for blockchain's core functionality.
   *
   * Just like in the `flon.bios` sample contract implementation, there are a few actions which are not implemented at the contract level (`newaccount`, `updateauth`, `deleteauth`, `linkauth`, `unlinkauth`, `canceldelay`, `onerror`, `setabi`, `setcode`), they are just declared in the contract so they will show in the contract's ABI and users will be able to push those actions to the chain via the account holding the `flon.system` contract, but the implementation is at the FULLON core level. They are referred to as FULLON native actions.
   *
   * - Users can buy gas, stake tokens for voting, and vote for producers.
   * - Producers register in order to be voted for, and can claim per-block and per-vote rewards.
   * - Users can buy and sell RAM at a market-determined price.
   * - Users can bid on premium names.
   */

   #ifdef ENABLE_NAME_BID
   // A name bid, which consists of:
   // - a `newname` name that the bid is for
   // - a `high_bidder` account name that is the one with the highest bid so far
   // - the `high_bid` which is amount of highest bid
   // - and `last_bid_time` which is the time of the highest bid
   struct [[eosio::table, eosio::contract("flon.system")]] name_bid {
     name            newname;
     name            high_bidder;
     int64_t         high_bid = 0; ///< negative high_bid == closed auction waiting to be claimed
     time_point      last_bid_time;

     uint64_t primary_key()const { return newname.value;                    }
     uint64_t by_high_bid()const { return static_cast<uint64_t>(-high_bid); }
   };

   // A bid refund, which is defined by:
   // - the `bidder` account name owning the refund
   // - the `amount` to be refunded
   struct [[eosio::table, eosio::contract("flon.system")]] bid_refund {
      name         bidder;
      asset        amount;

      uint64_t primary_key()const { return bidder.value; }
   };
   typedef eosio::multi_index< "namebids"_n, name_bid,
                               indexed_by<"highbid"_n, const_mem_fun<name_bid, uint64_t, &name_bid::by_high_bid>  >
                             > name_bid_table;

   typedef eosio::multi_index< "bidrefunds"_n, bid_refund > bid_refund_table;
   #endif//ENABLE_NAME_BID

   // Defines new global state parameters.
   struct [[eosio::table("global"), eosio::contract("flon.system")]] eosio_global_state : eosio::blockchain_parameters {
      uint64_t free_ram()const { return max_ram_size - total_ram_bytes_reserved; }

      asset                total_vote_stake;
      uint64_t             max_ram_size = 64ll*1024 * 1024 * 1024;
      uint64_t             total_ram_bytes_reserved = 0;
      int64_t              total_ram_stake = 0;

      // time_point           last_pervote_bucket_fill;
      time_point           election_activated_time;            /// election activated time
      time_point           reward_started_time;                /// election activated time
      asset                initial_rewards_per_block;          /// initial reward per block
      asset                total_produced_rewards;             /// total produced rewards
      asset                total_unclaimed_rewards;            /// all rewards which have been produced but not paid
      uint16_t             last_producer_schedule_size = 0;
      block_timestamp      last_producer_schedule_update;
      block_timestamp      last_name_close;
      uint8_t              revision = 0; ///< used to track version updates in the future.


      // explicit serialization macro is not necessary, used here only to improve compilation time
      EOSLIB_SERIALIZE_DERIVED( eosio_global_state, eosio::blockchain_parameters,
                                (total_vote_stake)(max_ram_size)(total_ram_bytes_reserved)(total_ram_stake)
                                (election_activated_time)(reward_started_time)(initial_rewards_per_block)
                                (total_produced_rewards)(total_unclaimed_rewards)
                                (last_producer_schedule_size)(last_producer_schedule_update)
                                (last_name_close)(revision) )
   };

   inline eosio::block_signing_authority convert_to_block_signing_authority( const eosio::public_key& producer_key ) {
      return eosio::block_signing_authority_v0{ .threshold = 1, .keys = {{producer_key, 1}} };
   }

   // User account information about:
   // Scope is current contract account
   // - `owner` the user account name
   // - `creator` the creator account name
   // struct [[eosio::table, eosio::contract("flon.system")]] account_creator {
   //    name                 owner;                     /// the user account name
   //    name                 creator;                   /// the creator account name
   //    uint8_t              revision              = 0; ///< used to track version updates in the future.

   //    uint64_t primary_key()const { return owner.value; }
   // };

   struct [[eosio::table, eosio::contract("flon.system")]] account_creator {
      name                 owner;                     /// the user account name
      name                 creator;                   /// the creator account name

      uint64_t primary_key()const { return owner.value; }
   };

   // typedef eosio::multi_index< "users"_n, account_creator >  creators_table;
   typedef eosio::multi_index< "creators"_n, account_creator >  creators_table;

   #ifdef ENABLE_VOTING_PRODUCER
   // Defines `producer_info` structure to be stored in `producer_info` table, added after version 1.0
   struct [[eosio::table, eosio::contract("flon.system")]] producer_info {
      name                             owner;
      int64_t                          total_votes = 0;
      eosio::public_key                producer_key; /// a packed public key object
      bool                             is_active = true;
      std::string                      url;
      asset                            unclaimed_rewards;
      time_point                       last_claim_time;
      uint16_t                         location = 0;
      eosio::block_signing_authority   producer_authority;
      uint32_t                         reward_shared_ratio  = 0; //reward shared ratio
      uint8_t                          revision = 0; ///< used to track version updates in the future.
      binary_extension<std::string>    status_url; ///< public producer status and incident page

      uint64_t primary_key()const { return owner.value;                             }
      uint64_t  by_votes()const    {
          return is_active ?
                           uint64_t(std::numeric_limits<int64_t>::max() - total_votes) :
                           std::numeric_limits<uint64_t>::max() - (uint64_t)total_votes;
      }
      bool     active()const      { return is_active;                               }
      void     deactivate()       {
         producer_key = public_key();
         producer_authority = eosio::block_signing_authority{};
         is_active = false;
      }
   };

   // finalizer_key_info stores information about a finalizer key.
   struct [[eosio::table("finkeys"), eosio::contract("flon.system")]] finalizer_key_info {
      uint64_t          id;                   // automatically generated ID for the key in the table
      name              finalizer_name;       // name of the finalizer owning the key
      std::string       finalizer_key;        // finalizer key in base64url format
      std::vector<char> finalizer_key_binary; // finalizer key in binary format in Affine little endian non-montgomery g1

      uint64_t    primary_key() const { return id; }
      uint64_t    by_fin_name() const { return finalizer_name.value; }
      // Use binary format to hash. It is more robust and less likely to change
      // than the base64url text encoding of it.
      // There is no need to store the hash key to avoid re-computation,
      // which only happens if the table row is modified. There won't be any
      // modification of the table rows of; it may only be removed.
      checksum256 by_fin_key()  const { return eosio::sha256(finalizer_key_binary.data(), finalizer_key_binary.size()); }

      bool is_active(uint64_t finalizer_active_key_id) const { return id == finalizer_active_key_id ; }
   };

   typedef eosio::multi_index<
      "finkeys"_n, finalizer_key_info,
      indexed_by<"byfinname"_n, const_mem_fun<finalizer_key_info, uint64_t, &finalizer_key_info::by_fin_name>>,
      indexed_by<"byfinkey"_n, const_mem_fun<finalizer_key_info, checksum256, &finalizer_key_info::by_fin_key>>
   > finalizer_keys_table;

   // finalizer_info stores information about a finalizer.
   struct [[eosio::table("finalizers"), eosio::contract("flon.system")]] finalizer_info {
      name              finalizer_name;           // finalizer's name
      uint64_t          active_key_id;            // finalizer's active finalizer key's id in finalizer_keys_table, for fast finding key information
      std::vector<char> active_key_binary;        // finalizer's active finalizer key's binary format in Affine little endian non-montgomery g1
      uint32_t          finalizer_key_count = 0;  // number of finalizer keys registered by this finalizer

      uint64_t primary_key() const { return finalizer_name.value; }
   };
   typedef eosio::multi_index< "finalizers"_n, finalizer_info > finalizers_table;

   // finalizer_auth_info stores a finalizer's key id and its finalizer authority
   struct finalizer_auth_info {
      finalizer_auth_info() = default;
      explicit finalizer_auth_info(const finalizer_info& finalizer);

      uint64_t                   key_id;        // A finalizer's key ID in finalizer_keys_table
      eosio::finalizer_authority fin_authority; // The finalizer's finalizer_authority

      bool operator==(const finalizer_auth_info& other) const {
         // Weight and description can never be changed by a user.
         // They are not considered here.
         return key_id == other.key_id &&
                fin_authority.public_key == other.fin_authority.public_key;
      };

      EOSLIB_SERIALIZE( finalizer_auth_info, (key_id)(fin_authority) )
   };

   // A single entry storing information about last proposed finalizers.
   // Should avoid  using the global singleton pattern as it unnecessarily
   // serializes data at construction/desstruction of system_contract,
   // even if the data is not used.
   struct [[eosio::table("lastpropfins"), eosio::contract("flon.system")]] last_prop_finalizers_info {
      std::vector<finalizer_auth_info> last_proposed_finalizers; // sorted by ascending finalizer key id

      uint64_t primary_key()const { return 0; }

      EOSLIB_SERIALIZE( last_prop_finalizers_info, (last_proposed_finalizers) )
   };

   typedef eosio::multi_index< "lastpropfins"_n, last_prop_finalizers_info >  last_prop_fins_table;

   // A single entry storing next available finalizer key_id to make sure
   // key_id in finalizers_table will never be reused.
   struct [[eosio::table("finkeyidgen"), eosio::contract("flon.system")]] fin_key_id_generator_info {
      uint64_t next_finalizer_key_id = 0;
      uint64_t primary_key()const { return 0; }

      EOSLIB_SERIALIZE( fin_key_id_generator_info, (next_finalizer_key_id) )
   };

   typedef eosio::multi_index< "finkeyidgen"_n, fin_key_id_generator_info >  fin_key_id_gen_table;

   // Voter info. Voter info stores information about the voter:
   // - `owner` the voter
   // - `producers` the producers approved by this voter
   // - `staked` the amount staked
   struct [[eosio::table, eosio::contract("flon.system")]] voter_info {
      name                owner;     /// the voter
      std::vector<name>   producers; /// the producers approved by this voter
      int64_t             votes                 = 0;  /// elected votes
      block_timestamp     last_unvoted_time;          /// vote updated time
      uint8_t             revision              = 0; ///< used to track version updates in the future.

      uint64_t primary_key()const { return owner.value; }
   };


   typedef eosio::multi_index< "voters"_n, voter_info >  voters_table;

   struct vote_allocation {
      name    producer;
      int64_t votes = 0;

      bool operator==(const vote_allocation& other) const {
         return producer == other.producer && votes == other.votes;
      }

      EOSLIB_SERIALIZE(vote_allocation, (producer)(votes))
   };

   struct [[eosio::table("voteallocs"), eosio::contract("flon.system")]] voter_allocations {
      name                         voter;
      std::vector<vote_allocation> allocations;

      uint64_t primary_key() const { return voter.value; }
   };
   typedef eosio::multi_index<"voteallocs"_n, voter_allocations> voter_allocations_table;

   struct [[eosio::table("pendallocs"), eosio::contract("flon.system")]] pending_voter_allocations {
      name                         voter;
      std::vector<vote_allocation> allocations;
      uint64_t                     activation_epoch = 0;

      uint64_t primary_key() const { return voter.value; }
   };
   typedef eosio::multi_index<"pendallocs"_n, pending_voter_allocations> pending_voter_allocations_table;

   typedef eosio::multi_index< "producers"_n, producer_info,
                               indexed_by<"prototalvote"_n, const_mem_fun<producer_info, uint64_t, &producer_info::by_votes>  >
                             > producers_table;
   #endif//ENABLE_VOTING_PRODUCER

   typedef eosio::singleton< "global"_n, eosio_global_state >   global_state_singleton;

   #ifdef ENABLE_VOTING_PRODUCER
   struct [[eosio::table, eosio::contract("flon.system")]] vote_refund {
      name            owner;
      time_point_sec  request_time;
      eosio::asset    vote_staked;

      uint64_t  primary_key()const { return owner.value; }

      // explicit serialization macro is not necessary, used here only to improve compilation time
      EOSLIB_SERIALIZE( vote_refund, (owner)(request_time)(vote_staked) )
   };

   typedef eosio::multi_index< "voterefund"_n, vote_refund >      vote_refund_table;

   struct [[eosio::table("bpstakecfg"), eosio::contract("flon.system")]] bp_stake_config {
      bool     enforce_schedule = false;
      asset    min_self_stake;
      uint32_t unstake_delay_sec = 21 * 24 * 60 * 60;
   };
   typedef eosio::singleton<"bpstakecfg"_n, bp_stake_config> bp_stake_config_singleton;

   struct [[eosio::table, eosio::contract("flon.system")]] bp_self_stake {
      name                       owner;
      asset                      staked;
      binary_extension<uint64_t> generation;
      binary_extension<bool>     committed;

      uint64_t primary_key() const { return owner.value; }
   };
   typedef eosio::multi_index<"bpstakes"_n, bp_self_stake> bp_self_stake_table;

   struct [[eosio::table, eosio::contract("flon.system")]] bp_stake_refund {
      name                       owner;
      asset                      quantity;
      time_point_sec             available_at;
      binary_extension<uint64_t> generation;

      uint64_t primary_key() const { return owner.value; }
   };
   typedef eosio::multi_index<"bpstkrefund"_n, bp_stake_refund> bp_stake_refund_table;

   static constexpr uint32_t legacy_reward_epoch_slots = 6 * 60 * 60 * 2;
   // New reward accounting is sealed hourly. Claim frequency is independent:
   // a producer claiming daily consumes about 24 completed hourly records.
   static constexpr uint32_t reward_epoch_seconds = 60 * 60;
   static constexpr uint32_t reward_epoch_slots = reward_epoch_seconds * 2;
   // Keep native schedule reconstruction in onblock tightly bounded. Four
   // minutes covers ordinary producer/network interruptions without allowing a
   // long outage to turn the next block into a large linear-time operation.
   static constexpr uint32_t producer_stats_onblock_limit = 480;
   static constexpr uint32_t max_schedule_candidates_scanned = 256;
   static constexpr uint32_t min_expected_blocks = 12;
   static constexpr uint32_t probation_missed_streak_limit = 12;
   static constexpr uint32_t max_suspension_epochs = 8;

   enum class bp_performance_status : uint8_t {
      unassessed          = 0,
      qualified          = 1,
      degraded           = 2,
      warning            = 3,
      suspended          = 4,
      insufficient_sample = 5,
      network_incident   = 6,
      probation          = 7
   };

   struct [[eosio::table, eosio::contract("flon.system")]] bp_performance {
      name     producer;
      uint64_t epoch_id = 0;
      uint64_t expected_blocks = 0;
      uint64_t produced_blocks = 0;
      uint64_t missed_blocks = 0;
      uint64_t longest_missed_streak = 0;
      uint64_t trailing_missed_streak = 0;
      uint8_t  status = static_cast<uint8_t>(bp_performance_status::unassessed);
      uint32_t warning_count = 0;
      binary_extension<uint64_t> idle_skipped_blocks;

      uint64_t primary_key() const { return producer.value; }
   };
   typedef eosio::multi_index<"bpstats"_n, bp_performance> bp_performance_table;

   struct [[eosio::table, eosio::contract("flon.system")]] epoch_performance {
      uint64_t          epoch_id = 0;
      uint32_t          start_slot = 0;
      uint32_t          end_slot = 0;
      eosio::checksum256 schedule_hash;
      bool              complete_sample = false;
      uint32_t          active_bp_count = 0;
      uint32_t          failed_bp_count = 0;
      bool              network_incident = false;
      bool              closed = false;

      uint64_t primary_key() const { return epoch_id; }
   };
   typedef eosio::multi_index<"epochstats"_n, epoch_performance> epoch_performance_table;

   struct [[eosio::table, eosio::contract("flon.system")]] producer_performance_state {
      name     producer;
      uint8_t  status = static_cast<uint8_t>(bp_performance_status::unassessed);
      uint32_t warning_count = 0;
      uint64_t suspended_until_epoch = 0;
      uint32_t suspension_count = 0;
      uint64_t probation_started_epoch = 0;
      binary_extension<uint32_t> probation_missed_streak;

      uint64_t primary_key() const { return producer.value; }
   };
   typedef eosio::multi_index<"bpstatus"_n, producer_performance_state> producer_performance_state_table;

   struct [[eosio::table("perfstate"), eosio::contract("flon.system")]] performance_accounting_state {
      uint32_t last_accounted_slot = 0;
      // Version the singleton so a contract upgrade can safely re-bootstrap
      // accounting instead of treating stale state as the current head slot.
      binary_extension<uint8_t> version;
   };
   typedef eosio::singleton<"perfstate"_n, performance_accounting_state> performance_accounting_singleton;

   struct [[eosio::table("electconfig"), eosio::contract("flon.system")]] election_config {
      uint16_t active_bp_quorum = 21;
   };
   typedef eosio::singleton<"electconfig"_n, election_config> election_config_singleton;

   struct [[eosio::table, eosio::contract("flon.system")]] epoch_reward {
      name     producer;
      asset    rewards;
      uint32_t reward_shared_ratio = 0;

      uint64_t primary_key() const { return producer.value; }
   };
   typedef eosio::multi_index<"epochreward"_n, epoch_reward> epoch_rewards_table;

   struct [[eosio::table, eosio::contract("flon.system")]] producer_epoch_claim {
      uint64_t epoch_id = 0;
      asset quantity;
      uint64_t primary_key() const { return epoch_id; }
   };
   typedef eosio::multi_index<"prodclaims"_n, producer_epoch_claim>
      producer_epoch_claims_table;

   struct [[eosio::table("rewardepoch"), eosio::contract("flon.system")]] reward_epoch_state {
      uint64_t next_epoch_to_close = 0;
   };
   typedef eosio::singleton<"rewardepoch"_n, reward_epoch_state> reward_epoch_singleton;

   // Append-only reward epoch schedule. Each row starts a new interpretation
   // at an explicit epoch/slot boundary; prior rows remain authoritative for
   // historical epochs.
   struct [[eosio::table, eosio::contract("flon.system")]] reward_epoch_plan {
      uint64_t first_epoch_id = 0;
      uint32_t start_slot = 0;
      uint32_t epoch_slots = reward_epoch_slots;

      uint64_t primary_key() const { return first_epoch_id; }
      uint64_t by_start_slot() const { return start_slot; }
   };
   typedef eosio::multi_index<
      "rewardplans"_n, reward_epoch_plan,
      indexed_by<"bystart"_n,
         const_mem_fun<reward_epoch_plan, uint64_t, &reward_epoch_plan::by_start_slot>>>
      reward_epoch_plans_table;

   // Read-only compatibility shape for chains that deployed the transitional
   // rewardcfgv2 singleton. It is intentionally omitted from the public ABI;
   // setrwdcfg migrates its boundary into rewardplans without rewriting history.
   struct legacy_reward_epoch_config_v2 {
      uint64_t first_epoch_id = 0;
      uint32_t start_slot = 0;
      uint32_t epoch_slots = reward_epoch_slots;
   };
   typedef eosio::singleton<"rewardcfgv2"_n, legacy_reward_epoch_config_v2>
      legacy_reward_epoch_config_v2_singleton;

   enum class reward_seal_phase : uint8_t { ending = 1, applying_deltas = 2, sealed = 3 };

   struct [[eosio::table("rewardseal"), eosio::contract("flon.system")]] reward_seal_state {
      uint64_t epoch_id = 0;
      uint8_t  phase = static_cast<uint8_t>(reward_seal_phase::sealed);
      uint8_t  stage = 0;
      name     cursor;
      uint32_t active_bp_count = 0;
      uint32_t failed_bp_count = 0;
      bool     network_incident = false;
   };
   typedef eosio::singleton<"rewardseal"_n, reward_seal_state> reward_seal_singleton;

   struct [[eosio::table, eosio::contract("flon.system")]] pending_voter_state {
      name              voter;
      int64_t           votes = 0;
      std::vector<name> producers;
      uint64_t          activation_epoch = 0;

      uint64_t primary_key() const { return voter.value; }
   };
   typedef eosio::multi_index<"pendingvote"_n, pending_voter_state> pending_voters_table;

   struct [[eosio::table, eosio::contract("flon.system")]] pending_producer_delta {
      name    producer;
      int64_t votes_delta = 0;

      uint64_t primary_key() const { return producer.value; }
   };
   typedef eosio::multi_index<"votedeltas"_n, pending_producer_delta> pending_vote_deltas_table;
   #endif//ENABLE_VOTING_PRODUCER


   // Defines new global state parameters.
   struct [[eosio::table("prodconf"), eosio::contract("flon.system")]] producing_config {

      uint64_t             idle_block_interval_ms = 0;         ///< idle block interval in milliseconds, 0 means disable idle block
   };


   typedef eosio::singleton< "prodconf"_n, producing_config >   producing_config_singleton;

   struct [[eosio::table("gasrewcfg"), eosio::contract("flon.system")]] gas_reward_config {
      bool enabled = false;
      // One hour at two slots per second. Existing on-chain configurations are
      // explicit rows and require a governed boundary migration.
      uint32_t epoch_slots = 7'200;
      uint16_t active_min_bp = 9'500;
      // Retains the serialized position of the removed standby-proof threshold.
      uint16_t reserved_standby_proofs = 0;
      uint16_t pool_bp = 1'000;
      uint16_t active_share_bp = 8'000;
      // Retains the serialized position of the removed base-share field so
      // existing on-chain singleton rows remain readable after the upgrade.
      uint16_t reserved_reward_bp = 0;
      uint32_t last_accounted_slot = 0;
      uint64_t last_metered_gas_value = 0;
      binary_extension<uint8_t> accounting_version;
      // Added at the tail as a binary extension so rows written by the
      // pre-reward-gating contract retain their serialized field offsets.
      binary_extension<uint64_t> first_reward_epoch;
   };
   using gas_reward_config_singleton = eosio::singleton<"gasrewcfg"_n, gas_reward_config>;

   struct [[eosio::table, eosio::contract("flon.system")]] gas_node_stat {
      name owner;
      uint8_t role = 0; // 1 active, 2 standby
      uint32_t expected = 0;
      uint32_t produced = 0;
      uint32_t missed = 0;
      uint32_t longest_missed = 0;
      // Retain removed standby-proof fields so existing rows remain readable.
      uint16_t reserved_standby_proofs = 0;
      uint32_t reserved_last_proof_slot = 0;
      checksum256 reserved_last_proof_block;
      uint64_t score = 0;
      bool eligible = false;
      asset allocated;
      binary_extension<uint64_t> idle_skipped;
      uint64_t primary_key() const { return owner.value; }
   };
   using gas_node_stats = eosio::multi_index<"gasnodestats"_n, gas_node_stat>;

   struct [[eosio::table, eosio::contract("flon.system")]] gas_epoch_config {
      uint64_t epoch_id;
      uint32_t epoch_slots;
      uint16_t active_min_bp;
      // Retains the serialized position of the removed standby-proof threshold.
      uint16_t reserved_standby_proofs = 0;
      uint16_t pool_bp;
      uint16_t active_share_bp;
      // Retains the serialized position of the removed base-share field.
      uint16_t reserved_reward_bp = 0;
      uint64_t primary_key() const { return epoch_id; }
   };
   using gas_epoch_configs = eosio::multi_index<"gasepochcfg"_n, gas_epoch_config>;

   struct [[eosio::table, eosio::contract("flon.system")]] gas_epoch_meter {
      uint64_t epoch_id;
      uint64_t gas_value = 0;
      uint64_t primary_key() const { return epoch_id; }
   };
   using gas_epoch_meters = eosio::multi_index<"gasepochmtr"_n, gas_epoch_meter>;

   struct [[eosio::table, eosio::contract("flon.system")]] gas_settlement {
      uint64_t epoch_id;
      asset pool;
      uint8_t phase = 1;
      name cursor;
      uint64_t active_score = 0;
      uint32_t active_total = 0;
      uint32_t active_eligible = 0;
      uint32_t standby_eligible = 0;
      uint32_t active_paid_count = 0;
      uint32_t standby_paid_count = 0;
      asset active_pool;
      asset standby_pool;
      asset active_allocated;
      asset standby_allocated;
      checksum256 allocation_hash;
      bool funded = false;
      bool incident_hold = false;
      bool finalized = false;
      // Added at the end for binary compatibility with existing rows. Counts
      // scheduled Active BPs that produced at least one block in the epoch.
      binary_extension<uint32_t> active_reward_participants;
      uint64_t primary_key() const { return epoch_id; }
   };
   using gas_settlements = eosio::multi_index<"gassettles"_n, gas_settlement>;

   struct [[eosio::table, eosio::contract("flon.system")]] gas_epoch_claim {
      uint64_t epoch_id = 0;
      asset quantity;
      uint64_t primary_key() const { return epoch_id; }
   };
   using gas_epoch_claims_table = eosio::multi_index<"gasclaims"_n, gas_epoch_claim>;

   struct [[eosio::table("gasautostate"), eosio::contract("flon.system")]] gas_auto_state {
      uint64_t next_epoch = 0;
      bool initialized = false;
   };
   using gas_auto_state_singleton = eosio::singleton<"gasautostate"_n, gas_auto_state>;

   /**
    * The `flon.system` smart contract is provided by `block.one` as a sample system contract, and it defines the structures and actions needed for blockchain's core functionality.
    *
    * Just like in the `flon.bios` sample contract implementation, there are a few actions which are not implemented at the contract level (`newaccount`, `updateauth`, `deleteauth`, `linkauth`, `unlinkauth`, `canceldelay`, `onerror`, `setabi`, `setcode`), they are just declared in the contract so they will show in the contract's ABI and users will be able to push those actions to the chain via the account holding the `flon.system` contract, but the implementation is at the FULLON core level. They are referred to as FULLON native actions.
    *
    * - Users can buy gas, stake tokens for voting, and vote for producers.
    * - Producers register in order to be voted for, and can claim per-block and per-vote rewards.
    * - Users can buy and sell RAM at a market-determined price.
    * - Users can bid on premium names.
    */
   class [[eosio::contract("flon.system")]] system_contract : public native {

      private:
         // creators_table             _users;
      #ifdef ENABLE_VOTING_PRODUCER
         voters_table             _voters;
         producers_table          _producers;
         finalizer_keys_table     _finalizer_keys;
         finalizers_table         _finalizers;
         last_prop_fins_table     _last_prop_finalizers;
         std::optional<std::vector<finalizer_auth_info>> _last_prop_finalizers_cached;
         fin_key_id_gen_table     _fin_key_id_generator;
         #endif//ENABLE_VOTING_PRODUCER
         global_state_singleton   _global;
         eosio_global_state       _gstate;

      public:
         static constexpr eosio::name active_permission     = {"active"_n};
         static constexpr eosio::name token_account         = {"flon.token"_n};
         static constexpr eosio::name gas_account           = {"flon.gas"_n};
         static constexpr eosio::name stake_account         = {"flon.stake"_n};
         static constexpr eosio::name vote_account          = {"flon.vote"_n};
         #ifdef ENABLE_VOTING_PRODUCER
         static constexpr eosio::name voter_reward_account  = {"voter.reward"_n};
         #endif//ENABLE_VOTING_PRODUCER
         static constexpr eosio::name names_account         = {"flon.names"_n};
         static constexpr eosio::name fees_account          = {"flon.fees"_n};
         static constexpr eosio::name null_account          = {"flon.null"_n};

         system_contract( name s, name code, datastream<const char*> ds );
         ~system_contract();

          // Returns the core symbol by system account name
          // @param system_account - the system account to get the core symbol for.
         static eosio::symbol get_core_symbol(const name& self) {
            global_state_singleton   global(self, self.value);
            check(global.exists(), "global does not exist");
            auto sym = global.get().total_vote_stake.symbol;
            check(sym.raw() != 0, "system contract must first be initialized");
            return sym;
         }

         inline static bool is_init(const name& self) {
            global_state_singleton   global(self, self.value);
            return global.exists() && global.get().total_vote_stake.symbol.is_valid();
         }

         // Actions:
         /**
          * The Init action initializes the system contract for a version and a symbol.
          * Only succeeds when:
          * - version is 0 and
          * - symbol is found and
          * - system token supply is greater than 0,
          * - and system contract wasn’t already been initialized.
          *
          * @param version - the version, has to be 0,
          * @param core - the system symbol.
          */
         [[eosio::action]]
         void init( unsigned_int version, const symbol& core );

         // [[eosio::action]]
         // void migrate( int max = 1000 ) {
         //    creators_table  creators( get_self(), get_self().value );
         //    creators_table_new  creators_new( get_self(), get_self().value );

         //    auto itr = creators.begin();
         //    if( itr == creators.end() ) {
         //       check( false, "no more records to migrate" );
         //    }

         //    while( itr != creators.end() && max-- > 0 ) {
         //       auto item = *itr;
         //       creators_new.emplace( item.owner, [&]( auto& row ) {
         //          row.owner     = item.owner;
         //          row.creator   = item.creator;
         //       });

         //       itr = creators.erase(itr);
         //    }
         // }

         #ifdef ENABLE_VOTING_PRODUCER
         /**
          * Configure vote-ranked producer scheduling and base block-production rewards.
          * Gas-funded FLON rewards are configured separately by setgasrwcfg.
          *
          * @param schedule_start - time at which vote-ranked producer schedules become active
          * @param reward_epoch_start - epoch-zero and base reward start; must not precede schedule_start
          * @param initial_block_reward - initial FLON reward generated per produced block
          */
         [[eosio::action]]
         void setprodcfg( const time_point& schedule_start, const time_point& reward_epoch_start,
                          const asset& initial_block_reward );

         /**
          * Schedule the one-hour reward epoch timeline for an already-running
          * chain at a future, legacy-epoch-aligned boundary.
          */
         [[eosio::action]]
         void setrwdcfg( const time_point& start_time );

         #endif//ENABLE_VOTING_PRODUCER

         /**
          * On block action. This special action is triggered when a block is applied by the given producer
          * and cannot be generated from any other source. It is used to pay producers and calculate
          * missed blocks of other producers. Producer pay is deposited into the producer's stake
          * balance and can be withdrawn over time. Once a minute, it may update the active producer config from the
          * producer votes. The action also populates the blockinfo table.
          *
          * @param header - the block header produced.
          */
         [[eosio::action]]
         void onblock( ignore<block_header> header );

         /**
          * Set account limits action sets the resource limits of an account
          *
          * @param account - name of the account whose resource limit to be set,
          * @param gas - reserved gas to be set,
          * @param is_unlimited - whether the account has unlimited resources.
          */
         [[eosio::action]]
         void setalimits( const name& account, uint64_t gas, bool is_unlimited );

         /**
          * The activate action, activates a protocol feature
          *
          * @param feature_digest - hash of the protocol feature to activate.
          */
         [[eosio::action]]
         void activate( const eosio::checksum256& feature_digest );

         /**
          * Logging for actions resulting in system fees.
          *
          * @param protocol - name of protocol fees were earned from.
          * @param fee - the amount of fees collected by system.
          * @param memo - (optional) the memo associated with the action.
          */
         [[eosio::action]]
         void logsystemfee( const name& protocol, const asset& fee, const std::string& memo );

         /**
          * Buy gas action, increases receiver's gas quota in ELON of quant
          *
          * @param payer - the gas buyer,
          * @param receiver - the gas receiver,
          * @param quant - the quantity of tokens to buy gas with.
          */
         [[eosio::action]]
         void buygas( const name& payer, const name& receiver, const asset& quant );

         /**
          * The buygasself action is designed to enhance the permission security by allowing an account to purchase GAS exclusively for itself.
          * This action prevents the potential risk associated with standard actions like buygas,
          * which can transfer CORE tokens out of the account, acting as a proxy for flon.token::transfer.
          *
          * @param account - the gas buyer and receiver,
          * @param quant - the quantity of tokens to buy gas with.
          */
         [[eosio::action]]
         void buygasself( const name& account, const asset& quant );

         #ifdef ENABLE_VOTING_PRODUCER
         /**
          * Refund vote action, this action is called after the subvote-period to claim all pending
          * staked core asset of substracted votes belonging to owner.
          *
          * @param owner - the owner of the tokens claimed.
          */
         [[eosio::action]]
         void voterefund( const name& owner );

         // functions defined in voting.cpp

         /**
          * Register producer action, indicates that a particular account wishes to become a producer,
          * this action will create a `producer_config` and a `producer_info` object for `producer` scope
          * in producers tables.
          *
          * @param producer - account registering to be a producer candidate,
          * @param producer_key - the public key of the block producer, this is the key used by block producer to sign blocks,
          * @param url - the url of the block producer, normally the url of the block producer presentation website,
          * @param location - is the country code as defined in the ISO 3166, https://en.wikipedia.org/wiki/List_of_ISO_3166_country_codes
          * @param reward_shared_ratio - reward shared ratio
          * @param status_url - public status and incident page of the block producer
          *
          * @pre Producer to register is an account
          * @pre Authority of producer to register
          */
         [[eosio::action]]
         void regproducer( const name& producer, const public_key& producer_key, const string& url,
                           uint16_t location, optional<uint32_t> reward_shared_ratio,
                           binary_extension<string> status_url );

         /**
          * Register producer action, indicates that a particular account wishes to become a producer,
          * this action will create a `producer_config` and a `producer_info` object for `producer` scope
          * in producers tables.
          *
          * @param producer - account registering to be a producer candidate,
          * @param producer_authority - the weighted threshold multisig block signing authority of the block producer used to sign blocks,
          * @param url - the url of the block producer, normally the url of the block producer presentation website,
          * @param location - is the country code as defined in the ISO 3166, https://en.wikipedia.org/wiki/List_of_ISO_3166_country_codes
          * @param reward_shared_ratio - reward shared ratio
          * @param status_url - public status and incident page of the block producer
          *
          * @pre Producer to register is an account
          * @pre Authority of producer to register
          */
         [[eosio::action]]
         void regproducer2(   const name& producer, const block_signing_authority& producer_authority,
                              const string& url, uint16_t location, optional<uint32_t> reward_shared_ratio,
                              binary_extension<string> status_url );

         /**
          * Unregister producer action, deactivates the block producer with account name `producer`.
          *
          * Deactivate the block producer with account name `producer`.
          * @param producer - the block producer account to unregister.
          */
         [[eosio::action]]
         void unregprod( const name& producer );

         /**
          * Request re-entry after a performance suspension. The cooldown must
          * have expired. Re-entry starts in probation and remains subject to
          * the deterministic consecutive-missed-slot circuit breaker.
          *
          * @param producer - producer requesting recovery.
          */
         [[eosio::action]]
         void resumeprod( const name& producer );

         /** Configure the target Active BP schedule size during bootstrap. */
         [[eosio::action]]
         void setbpquorum( uint16_t active_bp_quorum );

         /** Configure BP self-stake qualification and its withdrawal delay. */
         [[eosio::action]]
         void setbpstake( bool enforce_schedule, const asset& min_self_stake,
                          uint32_t unstake_delay_sec );

         /** Lock FLON as the producer candidate's self-stake. */
         [[eosio::action]]
         void stakebp( const name& owner, const asset& quantity );

         /** Move unlocked excess, or all stake after unregistering, into the delayed refund queue. */
         [[eosio::action]]
         void unstakebp( const name& owner, const asset& quantity );

         /** Claim a matured BP self-stake refund. */
         [[eosio::action]]
         void claimbpstake( const name& owner );

         /** Cancel a pending BP self-stake refund and restore it to the same lifecycle. */
         [[eosio::action]]
         void cancelbpref( const name& owner );

         /**
          * Action to permanently transition to Savanna consensus.
          * Create the first generation of finalizer policy and activate
          * the policy by using `set_finalizers` host function
          *
          * @pre Require the authority of the contract itself
          * @pre A sufficient numner of the top 21 block producers have registered a finalizer key
          */
         [[eosio::action]]
         void switchtosvnn();

         /**
          * Action to register a finalizer key by a registered producer.
          * If this was registered before (and still exists) even
          * by other block producers, the registration will fail.
          * If this is the first registered finalizer key of the producer,
          * it will also implicitly be marked active.
          * A registered producer can have multiple registered finalizer keys.
          *
          * @param finalizer_name - account registering `finalizer_key`,
          * @param finalizer_key - key to be registered. The key is in base64url format.
          * @param proof_of_possession - a valid Proof of Possession signature to show the producer owns the private key of the finalizer_key. The signature is in base64url format.
          *
          * @pre `finalizer_name` must be a registered producer
          * @pre `finalizer_key` must be in base64url format
          * @pre `proof_of_possession` must be a valid of proof of possession signature
          * @pre Authority of `finalizer_name` to register. `linkauth` may be used to allow a lower authrity to exectute this action.
          */
         [[eosio::action]]
         void regfinkey( const name& finalizer_name, const std::string& finalizer_key, const std::string& proof_of_possession);

         /**
          * Action to activate a finalizer key. If the finalizer is currently an
          * active block producer (in top 21), then immediately change Finalizer Policy.
          * A finalizer may only have one active finalizer key. Activating a
          * finalizer key implicitly deactivates the previously active finalizer
          * key of that finalizer.
          *
          * @param finalizer_name - account activating `finalizer_key`,
          * @param finalizer_key - key to be activated.
          *
          * @pre `finalizer_key` must be a registered finalizer key in base64url format
          * @pre Authority of `finalizer_name`
          */
         [[eosio::action]]
         void actfinkey( const name& finalizer_name, const std::string& finalizer_key );

         /**
          * Action to delete a finalizer key. An active finalizer key may not be deleted
          * unless it is the last registered finalizer key. If it is the last one,
          * it will be deleted.
          *
          * @param finalizer_name - account deleting `finalizer_key`,
          * @param finalizer_key - key to be deleted.
          *
          * @pre `finalizer_key` must be a registered finalizer key in base64url format
          * @pre `finalizer_key` must not be active, unless it is the last registered finalizer key
          * @pre Authority of `finalizer_name`
          */
         [[eosio::action]]
         void delfinkey( const name& finalizer_name, const std::string& finalizer_key );

         /**
          * Vote producer action, votes for a set of producers. This action updates the list of `producers` voted for,
          * for `voter` account.
          * Storage change is billed to `voter`.
          *
          * @param voter - the account to change the voted producers for,
          * @param producers - the list of producers to vote for, a maximum of 30 producers is allowed.
          *
          * @pre Producers must be sorted from lowest to highest and must be registered and active
          * @pre Voter must authorize this action
          * @pre Voter must have previously staked some EOS for voting
          * @pre Voter->staked must be up to date
          *
          * @post Previous allocations are removed and the voter's aggregate vote amount is divided equally
          *       among the newly selected producers. Integer remainder stays unallocated.
          */
         [[eosio::action]]
         void voteproducer( const name& voter, const std::vector<name>& producers );

         /** Assign an explicit, conserved FLON vote amount to each producer. */
         [[eosio::action]]
         void votealloc( const name& voter, const std::vector<vote_allocation>& allocations );

         /**
          * Vote producer action, new version, votes for a set of producers. This action updates the list of `producers` voted for,
          * for `voter` account. Voter can vote for a list of at most 30 producers.
          * Storage change is billed to `voter`.
          *
          * @param voter - the account to change the voted producers for,
          * @param producers - the list of producers to vote for, a maximum of 30 producers is allowed.
          *
          * @pre Producers must be sorted from lowest to highest and must be registered and active
          * @pre Every listed producer must have been previously registered
          * @pre Voter must authorize this action
          * @pre Voter must have previously staked some EOS for voting
          * @pre Voter->votes must be positive
          * @pre Voter can only update votes once a day, restricted actions: (addvote, subvote, vote)
          *
          * @post Every producer previously voted for will have vote reduced by previous vote amount
          * @post Every producer newly voted for will have vote increased by new vote amount
          */
         // [[eosio::action]]
         // void vote( const name& voter, const std::vector<name>& producers );

         /**
          * Add vote action. This action add voter's `votes` for voting,
          * Storage change is billed to `voter`.
          *
          * @param voter - the voter account,
          * @param vote_staked - the core asset to stake for addding votes. Asset amount must be positive
          *
          * @pre Voter must authorize this action
          * @pre Voter must have enough core asset to stake for adding votes
          * @pre Voter can only update votes once a day, restricted actions: (addvote, subvote, vote)
          *
          * @post All producers `voter` account has voted for will have their votes updated immediately.
          */
         [[eosio::action]]
         void addvote( const name& voter, const asset& vote_staked );

         /**
          * Sub vote action. This action substract voter's `votes` for voting,
          * Storage change is billed to `voter`.
          *
          * @param voter - the voter account,
          * @param vote_staked - the core asset to stake for substracting votes. Asset amount must be positive
          *
          * @pre Voter must authorize this action
          * @pre Voter must have enough votes to substract
          * @pre Voter can only have one substracted votes at a time (including processing of pending refunds)
          * @pre Voter can only update votes once a day, restricted actions: (addvote, subvote, vote)
          *
          * @post The substracting staked will be transferred to `voter` liquid balance by calling
          *    `voterefund` after the refund delay.
          * @post All producers `voter` account has voted for will have their votes updated immediately.
          */
         [[eosio::action]]
         void subvote( const name& voter, const asset& vote_staked );
         #else

         /**
          * Set producers action, sets a new list of active producers, by proposing a schedule change, once the block that
          * contains the proposal becomes irreversible, the schedule is promoted to "pending"
          * automatically. Once the block that promotes the schedule is irreversible, the schedule will
          * become "active".
          *
          * @param schedule - New list of active producers to set
          */
         [[eosio::action]]
         void setprods( const std::vector<eosio::producer_authority>& schedule );

         #endif//ENABLE_VOTING_PRODUCER

         /**
          * Set the blockchain parameters. By tunning these parameters a degree of
          * customization can be achieved.
          * @param params - New blockchain parameters to set.
          */
         [[eosio::action]]
         void setparams( const blockchain_parameters_t& params );

#ifdef SYSTEM_CONFIGURABLE_WASM_LIMITS
         /**
          * Sets the WebAssembly limits.  Valid parameters are "low",
          * "default" (equivalent to low), and "high".  A value of "high"
          * allows larger contracts to be deployed.
          */
         [[eosio::action]]
         void wasmcfg( const name& settings );
#endif

         #ifdef ENABLE_VOTING_PRODUCER
         /**
          * Claim rewards action, claims block producing and vote rewards.
          * @param owner - producer account claiming per-block and per-vote rewards.
          */
         [[eosio::action]]
         void claimrewards( const name& caller, const name& producer, uint16_t max_steps );

         /**
          * Undo reward action, undo the produced block rewards.
          * @param owner - producer account.
          */
         // [[eosio::action]]
         // void undoreward( const name& owner, const asset& rewards );
         #endif//ENABLE_VOTING_PRODUCER

         /**
          * Set privilege status for an account. Allows to set privilege status for an account (turn it on/off).
          * @param account - the account to set the privileged status for.
          * @param is_priv - 0 for false, > 0 for true.
          */
         [[eosio::action]]
         void setpriv( const name& account, uint8_t is_priv );

         #ifdef ENABLE_VOTING_PRODUCER
         /**
          * Remove producer action, deactivates a producer by name, if not found asserts.
          * @param producer - the producer account to deactivate.
          */
         [[eosio::action]]
         void rmvproducer( const name& producer );
         #endif//ENABLE_VOTING_PRODUCER

         #ifdef ENABLE_NAME_BID
         /**
          * Bid name action, allows an account `bidder` to place a bid for a name `newname`.
          * @param bidder - the account placing the bid,
          * @param newname - the name the bid is placed for,
          * @param bid - the amount of system tokens payed for the bid.
          *
          * @pre Bids can be placed only on top-level suffix,
          * @pre Non empty name,
          * @pre Names longer than 12 chars are not allowed,
          * @pre Names equal with 12 chars can be created without placing a bid,
          * @pre Bid has to be bigger than zero,
          * @pre Bid's symbol must be system token,
          * @pre Bidder account has to be different than current highest bidder,
          * @pre Bid must increase current bid by 10%,
          * @pre Auction must still be opened.
          */
         [[eosio::action]]
         void bidname( const name& bidder, const name& newname, const asset& bid );

         /**
          * Bid refund action, allows the account `bidder` to get back the amount it bid so far on a `newname` name.
          *
          * @param bidder - the account that gets refunded,
          * @param newname - the name for which the bid was placed and now it gets refunded for.
          */
         [[eosio::action]]
         void bidrefund( const name& bidder, const name& newname );
         #endif//ENABLE_NAME_BID

         // /**
         //  * Config reward parameters
         //  * Only be set after contract init() and before reward start.
         //  *
         //  * @param init_reward_start_time - start time of initializing reward phase.
         //  * @param init_reward_end_time - end time of initializing reward phase.
         //  * @param main_rewards_per_block - rewards per block of main producers in initializing reward phase.
         //  * @param backup_rewards_per_block - rewards per block of backup producers in initializing reward phase.
         //  */
         // [[eosio::action]]
         // void cfgreward( const time_point& init_reward_start_time, const time_point& init_reward_end_time,
         //                 const asset& main_rewards_per_block, const asset& backup_rewards_per_block );

         /**
          * limitauthchg opts into or out of restrictions on updateauth, deleteauth, linkauth, and unlinkauth.
          *
          * If either allow_perms or disallow_perms is non-empty, then opts into restrictions. If
          * allow_perms is non-empty, then the authorized_by argument of the restricted actions must be in
          * the vector, or the actions will abort. If disallow_perms is non-empty, then the authorized_by
          * argument of the restricted actions must not be in the vector, or the actions will abort.
          *
          * If both allow_perms and disallow_perms are empty, then opts out of the restrictions. limitauthchg
          * aborts if both allow_perms and disallow_perms are non-empty.
          *
          * @param account - account to change
          * @param allow_perms - permissions which may use the restricted actions
          * @param disallow_perms - permissions which may not use the restricted actions
          */
         [[eosio::action]]
         void limitauthchg( const name& account, const std::vector<name>& allow_perms, const std::vector<name>& disallow_perms );

         /**
          * Set producing configuration
          *
          * @param idle_block_interval_ms - the idle block interval in milliseconds. 0 means disable idle block.
          */
         [[eosio::action]]
         void setibintervl( uint64_t idle_block_interval_ms );

         [[eosio::action]]
         void setgasrwcfg(bool enabled, uint32_t epoch_slots, uint16_t active_min_bp,
                          uint16_t pool_bp, uint16_t active_share_bp);

         [[eosio::action]] void resolveincdt(uint64_t epoch_id, bool proceed);

         using init_action = eosio::action_wrapper<"init"_n, &system_contract::init>;
         using activate_action = eosio::action_wrapper<"activate"_n, &system_contract::activate>;
         using logsystemfee_action = eosio::action_wrapper<"logsystemfee"_n, &system_contract::logsystemfee>;
         using buygas_action = eosio::action_wrapper<"buygas"_n, &system_contract::buygas>;
         #ifdef ENABLE_VOTING_PRODUCER
         using regproducer_action = eosio::action_wrapper<"regproducer"_n, &system_contract::regproducer>;
         using regproducer2_action = eosio::action_wrapper<"regproducer2"_n, &system_contract::regproducer2>;
         using unregprod_action = eosio::action_wrapper<"unregprod"_n, &system_contract::unregprod>;
         using resumeprod_action = eosio::action_wrapper<"resumeprod"_n, &system_contract::resumeprod>;
         using setbpquorum_action = eosio::action_wrapper<"setbpquorum"_n, &system_contract::setbpquorum>;
         using setbpstake_action = eosio::action_wrapper<"setbpstake"_n, &system_contract::setbpstake>;
         using stakebp_action = eosio::action_wrapper<"stakebp"_n, &system_contract::stakebp>;
         using unstakebp_action = eosio::action_wrapper<"unstakebp"_n, &system_contract::unstakebp>;
         using claimbpstake_action = eosio::action_wrapper<"claimbpstake"_n, &system_contract::claimbpstake>;
         using cancelbpref_action = eosio::action_wrapper<"cancelbpref"_n, &system_contract::cancelbpref>;
         using voteproducer_action = eosio::action_wrapper<"voteproducer"_n, &system_contract::voteproducer>;
         using votealloc_action = eosio::action_wrapper<"votealloc"_n, &system_contract::votealloc>;
         // using voteupdate_action = eosio::action_wrapper<"voteupdate"_n, &system_contract::voteupdate>;
         using claimrewards_action = eosio::action_wrapper<"claimrewards"_n, &system_contract::claimrewards>;
         using setprodcfg_action = eosio::action_wrapper<"setprodcfg"_n, &system_contract::setprodcfg>;
         using setrwdcfg_action = eosio::action_wrapper<"setrwdcfg"_n, &system_contract::setrwdcfg>;
         using rmvproducer_action = eosio::action_wrapper<"rmvproducer"_n, &system_contract::rmvproducer>;
         #endif//ENABLE_VOTING_PRODUCER
         #ifdef ENABLE_NAME_BID
         using bidname_action = eosio::action_wrapper<"bidname"_n, &system_contract::bidname>;
         using bidrefund_action = eosio::action_wrapper<"bidrefund"_n, &system_contract::bidrefund>;
         #endif//ENABLE_NAME_BID
         using setpriv_action = eosio::action_wrapper<"setpriv"_n, &system_contract::setpriv>;
         using setalimits_action = eosio::action_wrapper<"setalimits"_n, &system_contract::setalimits>;
         using setparams_action = eosio::action_wrapper<"setparams"_n, &system_contract::setparams>;
         // using cfgreward_action = eosio::action_wrapper<"cfgreward"_n, &system_contract::cfgreward>;
         using setibintervl_action = eosio::action_wrapper<"setibintervl"_n, &system_contract::setibintervl>;


      private:
         // Implementation details:

         const eosio::symbol& core_symbol() const {
            const auto& core_symbol = _gstate.total_vote_stake.symbol;
            check(core_symbol.is_valid(), "system contract must first be initialized");
            return core_symbol;
         }

         inline bool is_init() const {
            return _gstate.total_vote_stake.symbol.is_valid();
         }

         inline void check_init() const {
            check(is_init(), "system contract must first be initialized");
         }


         //defined in flon.system.cpp
         static eosio_global_state get_default_parameters();
         void channel_to_system_fees( const name& from, const asset& amount );
         void ensure_gas_epoch_roles(uint64_t epoch_id, const gas_reward_config& config);
         void accumulate_gas_node_stats(uint32_t current_slot);
         void accumulate_gas_slot_stats(
            uint32_t from_slot, uint32_t to_slot,
            const std::vector<eosio::producer_slot_stats_v2>& samples);
         void advance_gas_settlement(uint32_t current_slot);
         void beginsettle(uint64_t epoch_id);
         void settlenext(uint64_t epoch_id, uint16_t max_rows);
         void finalizeepoch(uint64_t epoch_id);


         #ifdef ENABLE_VOTING_PRODUCER
         // defined in voting.cpp
         void register_producer( const name& producer, const eosio::block_signing_authority& producer_authority,
                                 const std::string& url, uint16_t location, optional<uint32_t> reward_shared_ratio,
                                 const binary_extension<std::string>& status_url );
         bool has_min_bp_self_stake( const name& producer, const bp_stake_config& config ) const;
         void check_min_bp_self_stake( const name& producer, const bp_stake_config& config ) const;
         void update_elected_producers( const block_timestamp& timestamp );
         void update_vote_allocations( const std::vector<name>& old_producers, int64_t old_votes,
                                       const std::vector<name>& new_producers, int64_t new_votes );
         void update_producer_votes( const std::vector<name>& producers, int64_t votes_delta );
         void materialize_pending_vote( const name& voter );
         void queue_vote_change( const name& voter, int64_t votes,
                                 const std::vector<name>& producers, uint64_t activation_epoch );
         void update_pending_vote_deltas( const std::vector<name>& old_producers, int64_t old_votes,
                                          const std::vector<name>& new_producers, int64_t new_votes );
         std::vector<vote_allocation> equal_vote_allocations(const std::vector<name>& producers,
                                                              int64_t votes) const;
         std::vector<vote_allocation> target_vote_allocations(const name& voter,
                                                               const std::vector<name>& producers,
                                                               int64_t votes,
                                                               bool pending) const;
         void update_pending_allocation_deltas(const std::vector<vote_allocation>& old_allocations,
                                                const std::vector<vote_allocation>& new_allocations);
         uint64_t current_reward_epoch( const time_point& now ) const;
         uint64_t next_vote_epoch() const;
         uint64_t reward_epoch_for_slot( uint32_t slot ) const;
         uint32_t reward_epoch_start_slot( uint64_t epoch_id ) const;
         uint32_t reward_epoch_end_slot( uint64_t epoch_id ) const;
         void record_producer_slot_stats( const block_timestamp& timestamp );
         void accumulate_producer_slot_stats(
            uint64_t epoch_id, uint32_t from_slot, uint32_t to_slot,
            const std::vector<eosio::producer_slot_stats_v2>& samples );
         uint64_t suspension_cooldown_epochs( uint32_t suspension_count ) const;
         bool is_performance_eligible( const name& producer, uint64_t epoch_id ) const;
         uint32_t reward_factor_basis_points( const bp_performance* stats, bool network_incident ) const;
         void advance_reward_epoch_seal( uint16_t max_rows );
         void classify_producer_epoch( uint64_t epoch_id, const bp_performance& stats,
                                       bool complete_sample, bool network_incident );

         // defined in finalizer_key.cpp
         bool is_savanna_consensus();
         void set_proposed_finalizers( std::vector<finalizer_auth_info> finalizers );
         const std::vector<finalizer_auth_info>& get_last_proposed_finalizers();
         uint64_t get_next_finalizer_key_id();
         finalizers_table::const_iterator get_finalizer_itr( const name& finalizer_name ) const;

         #endif//ENABLE_VOTING_PRODUCER

         template <auto system_contract::*...Ptrs>
         class registration {
            public:
               template <auto system_contract::*P, auto system_contract::*...Ps>
               struct for_each {
                  template <typename... Args>
                  static constexpr void call( system_contract* this_contract, Args&&... args )
                  {
                     std::invoke( P, this_contract, args... );
                     for_each<Ps...>::call( this_contract, std::forward<Args>(args)... );
                  }
               };
               template <auto system_contract::*P>
               struct for_each<P> {
                  template <typename... Args>
                  static constexpr void call( system_contract* this_contract, Args&&... args )
                  {
                     std::invoke( P, this_contract, std::forward<Args>(args)... );
                  }
               };

               template <typename... Args>
               constexpr void operator() ( Args&&... args )
               {
                  for_each<Ptrs...>::call( this_contract, std::forward<Args>(args)... );
               }

               system_contract* this_contract;
         };

         // defined in power.cpp
         void adjust_resources(name payer, name account, symbol core_symbol, int64_t net_delta, int64_t cpu_delta, bool must_not_be_managed = false);

         // defined in block_info.cpp
         void add_to_blockinfo_table(const eosio::checksum256& previous_block_id, const eosio::block_timestamp timestamp) const;
   };

}

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

namespace eosiosystem {

   using std::string;
   using std::optional;
   using eosio::asset;
   using eosio::binary_extension;
   using eosio::block_timestamp;
   using eosio::check;
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
   static constexpr uint32_t refund_delay_sec      = 3 * seconds_per_day;

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
   struct [[eosio::table, eosio::contract("flon.system")]] user_account {
      name                 owner;                     /// the user account name
      name                 creator;                   /// the creator account name
      uint8_t              revision              = 0; ///< used to track version updates in the future.

      uint64_t primary_key()const { return owner.value; }
   };

   typedef eosio::multi_index< "users"_n, user_account >  users_table;

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
   #endif//ENABLE_VOTING_PRODUCER

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
         // users_table             _users;
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
         static constexpr eosio::name reward_account        = {"flon.reward"_n};
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


         #ifdef ENABLE_VOTING_PRODUCER
         // Actions:
         /**
          * The Init elect action initializes the election of producers.
          * Only succeeds when:
          * - has self auth
          * - symbol of initial_rewards_per_block is core symbol
          * - amount of initial_rewards_per_block is not negtive
          * - election_activated_time can not less than now when election wasn’t already been initialized
          * - election_activated_time can not be change when election was already been initialized
          * - reward_started_time can not less than election_activated_time
          * - reward_started_time can not less than now when reward wasn’t already been started
          * - reward_started_time it can not be change when reward was already been started
          *
          * @param election_activated_time - election activated time
          * @param core - reward per block.
          */
         [[eosio::action]]
         void cfgelection( const time_point& election_activated_time, const time_point& reward_started_time, const asset& initial_rewards_per_block);
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
          *
          * @pre Producer to register is an account
          * @pre Authority of producer to register
          */
         [[eosio::action]]
         void regproducer( const name& producer, const public_key& producer_key, const string& url,
                           uint16_t location, optional<uint32_t> reward_shared_ratio );

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
          *
          * @pre Producer to register is an account
          * @pre Authority of producer to register
          */
         [[eosio::action]]
         void regproducer2(   const name& producer, const block_signing_authority& producer_authority,
                              const string& url, uint16_t location, optional<uint32_t> reward_shared_ratio );

         /**
          * Unregister producer action, deactivates the block producer with account name `producer`.
          *
          * Deactivate the block producer with account name `producer`.
          * @param producer - the block producer account to unregister.
          */
         [[eosio::action]]
         void unregprod( const name& producer );

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
          * @post Every producer previously voted for will have vote reduced by previous vote weight
          * @post Every producer newly voted for will have vote increased by new vote amount
          */
         [[eosio::action]]
         void voteproducer( const name& voter, const std::vector<name>& producers );

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
          * @pre Voter can only have one substracted votes at a time (including processing of delayed refunds)
          * @pre Voter can only update votes once a day, restricted actions: (addvote, subvote, vote)
          *
          * @post The substracting staked will be transferred to `voter` liquid balance via a
          *    deferred `voterefund` transaction with a delay of 3 days.
          * @post All producers `voter` account has voted for will have their votes updated immediately.
          * @post Bandwidth and storage for the deferred transaction are billed to `voter`.
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
         void claimrewards( const name& owner );

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

         using init_action = eosio::action_wrapper<"init"_n, &system_contract::init>;
         using activate_action = eosio::action_wrapper<"activate"_n, &system_contract::activate>;
         using logsystemfee_action = eosio::action_wrapper<"logsystemfee"_n, &system_contract::logsystemfee>;
         using buygas_action = eosio::action_wrapper<"buygas"_n, &system_contract::buygas>;
         #ifdef ENABLE_VOTING_PRODUCER
         using regproducer_action = eosio::action_wrapper<"regproducer"_n, &system_contract::regproducer>;
         using regproducer2_action = eosio::action_wrapper<"regproducer2"_n, &system_contract::regproducer2>;
         using unregprod_action = eosio::action_wrapper<"unregprod"_n, &system_contract::unregprod>;
         using voteproducer_action = eosio::action_wrapper<"voteproducer"_n, &system_contract::voteproducer>;
         // using voteupdate_action = eosio::action_wrapper<"voteupdate"_n, &system_contract::voteupdate>;
         using claimrewards_action = eosio::action_wrapper<"claimrewards"_n, &system_contract::claimrewards>;
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


         #ifdef ENABLE_VOTING_PRODUCER
         // defined in voting.cpp
         void register_producer( const name& producer, const eosio::block_signing_authority& producer_authority,
                                 const std::string& url, uint16_t location, optional<uint32_t> reward_shared_ratio );
         void update_elected_producers( const block_timestamp& timestamp );
         void update_producer_votes( const std::vector<name>& producers, int64_t votes_delta,
                                             bool is_adding);

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

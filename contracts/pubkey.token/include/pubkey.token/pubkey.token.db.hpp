 #pragma once

#include <eosio/asset.hpp>
#include <eosio/privileged.hpp>
#include <eosio/singleton.hpp>
#include <eosio/system.hpp>
#include <eosio/time.hpp>
#include <utils.hpp>

#include <deque>
#include <optional>
#include <string>
#include <map>
#include <set>
#include <type_traits>

namespace flon {

using namespace std;
using namespace eosio;

#define SYMBOL(sym_code, precision) symbol(symbol_code(sym_code), precision)

static constexpr eosio::name active_perm{"active"_n};

static constexpr uint64_t percent_boost     = 10000;
static constexpr uint64_t max_memo_size     = 128;
static constexpr uint64_t max_addr_len      = 128;

static constexpr symbol FLON_SYMBOL         = SYMBOL("FLON", 4);
static constexpr name FLON_BANK             = "flon.token"_n;
static constexpr name FLON_SYSTEM           = "flonian"_n;

#define hash(str) sha256(const_cast<char*>(str.c_str()), str.size())

enum class err: uint8_t {
    NONE                = 0,
    RECORD_NOT_FOUND    = 1,
    RECORD_EXISTING     = 2,
    ADDRESS_ILLEGAL     = 3,
    SYMBOL_MISMATCH     = 4,
    ADDRESS_MISMATCH    = 5,
    NOT_COMMON_XIN      = 6,
    STATUS_INCORRECT    = 7,
    PARAM_INCORRECT     = 8,
    NO_AUTH             = 9,

};

#define TBL struct [[eosio::table, eosio::contract("pubkey.token")]]

struct [[eosio::table("global"), eosio::contract("pubkey.token")]] global_t {
    name        admin       = "flon.admin"_n;                 
    uint64_t    last_idx    = 0; 
    asset       miner_fee   = asset(1000, FLON_SYMBOL);

    EOSLIB_SERIALIZE( global_t, (admin)(last_idx)(miner_fee) )
};
typedef eosio::singleton< "global"_n, global_t > global_singleton;

TBL pubkey_account_t {
    uint64_t            id;
    eosio::public_key   pubkey;
    asset               quantity;
    time_point          last_recv_at;

    pubkey_account_t() {};

    uint64_t    primary_key()const { return id; }
    
    eosio::checksum256 by_pubkey() const {
        return eosio::sha256(reinterpret_cast<const char*>(&pubkey), sizeof(pubkey));
    }

    typedef eosio::multi_index<"pubkeyaccts"_n,  pubkey_account_t,
        indexed_by<"by.pubkey"_n, const_mem_fun<pubkey_account_t, checksum256, &pubkey_account_t::by_pubkey> >
    > idx_t;

    EOSLIB_SERIALIZE( pubkey_account_t, (id)(pubkey)(quantity)(last_recv_at) )
};


// TBL pubkey_t {
//     uint64_t            id;
//     eosio::public_key   pubkey1;
//     eosio::public_key   pubkey2;
//     eosio::public_key   pubkey3;

//     pubkey_t() {};

//     uint64_t    primary_key()const { return id; }
    

//     typedef eosio::multi_index<"pubkeys2"_n,  pubkey_t > idx_t;

//     EOSLIB_SERIALIZE( pubkey_t, (id)(pubkey1)(pubkey2)(pubkey3) )
// };

} // flon

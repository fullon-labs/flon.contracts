#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <string>
#include <wasm_db.hpp>
#include "pubkey.token/flon.token.hpp"
#include "pubkey.token/pubkey.token.db.hpp"

namespace flon {

using std::string;
using namespace eosio;
using namespace wasm::db;

/**
 * The `pubkey.token` is Cross-chain (X -> flon -> Y) contract
 * 
 */

#define TRANSFER(bank, from, to, quantity, memo) \
    {	token::transfer_action act{ bank, { {_self, active_perm} } };\
			act.send( from, to, quantity , memo );}


class [[eosio::contract("pubkey.token")]] pubkey_token : public contract {
private:
   dbc                      _db;
   global_singleton         _global;
   global_t                 _gstate;
   pubkey_account_t::idx_t  _tbl_pubkey_accts;

public:
   using contract::contract;

   pubkey_token(eosio::name receiver, eosio::name code, datastream<const char*> ds):
        _db(_self), contract(receiver, code, ds), 
        _tbl_pubkey_accts(get_self(), get_self().value),
        _global(_self, _self.value){
            
        if (_global.exists()) {
            _gstate = _global.get();

        } else { // first init
            _gstate = global_t{};
            _gstate.admin = _self;
        }
    }

    ~pubkey_token() { 
        _global.set( _gstate, get_self() );
    }

    // user -> pubkey.token, memo: $pubkey
    [[eosio::on_notify("*::transfer")]]
    void ontransfer( name from, name to, asset quantity, string memo );
    
    
    /**
     * @usage: create a new account, signed & submitted by a proxy miner
     * @params:
     *   - sig: singature by user:  acct | pubkey
     **/
    ACTION newaccount(const name& miner, const eosio::public_key& pubkey, const name& acct, const eosio::signature& sig);

    /**
     * usage: signed & submitted by proxy miner
     * params: 
     * 
     * sig: sign last_recv_at + to_acct string
     **/
    ACTION move(const name& miner, const eosio::public_key& pubkey, const time_point& last_recv_at, const name& to_acct, const eosio::signature& sig);

    ACTION checkpubkey(const string& pubkey_str, eosio::public_key& pubkey); 
    
    ACTION init( const name& admin) {
        _check_admin( );
        _gstate.admin  = admin;
    }

    private:
    void _check_admin(){
        CHECKC( has_auth(_self) || has_auth(_gstate.admin), err::NO_AUTH, "no auth for operate" )
    }

    void _on_pubkey_recv_token(const public_key& pubkey, const asset& quantity);

};
} //namespace apollo

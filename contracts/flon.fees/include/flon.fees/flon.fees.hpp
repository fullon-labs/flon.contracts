#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <flon.system/flon.system.hpp>

#include <string>

namespace eosiosystem {
   class system_contract;
}

namespace eosio {

   using std::string;
   /**
    * The flon.fees smart contract facilitates the collection of transaction fees from system accounts and their subsequent distribution to the Resource Exchange (REX) pool.
    *
    * This contract serves as an essential component for inclusion in system-level unit tests.
    *
    * A comprehensive implementation of the flon.fees contract can be accessed at EOS Network Foundation GitHub repository.
    * https://github.com/fullon-labs/flon.fees
    */
   class [[eosio::contract("flon.fees")]] fees : public contract {
      public:
         using contract::contract;

         [[eosio::on_notify("flon.token::transfer")]]
         void on_transfer( const name from, const name to, const asset quantity, const string memo );

         [[eosio::action]]
         void noop();
   };

}

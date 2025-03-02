#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>

#include <string>
namespace flon {

  class [[eosio::contract("flon.system")]] flon_system : public contract {
    public:
         using contract::contract;

        [[eosio::action]]
        void claimrewards( const name& submitter, const name& owner );
        using claimrewards_action = eosio::action_wrapper<"claimrewards"_n, &flon_system::claimrewards>;

  };
}

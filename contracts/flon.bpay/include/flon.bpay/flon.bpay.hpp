#pragma once

#include <eosio/eosio.hpp>
#include <flon.system/flon.system.hpp>
#include <flon.token/flon.token.hpp>

using namespace std;

namespace eosio {
    /**
     * The `flon.bpay` contract handles system bpay distribution.
     */
    class [[eosio::contract("flon.bpay")]] bpay : public contract {
    public:
        using contract::contract;

        /**
         * ## TABLE `rewards`
         *
         * @param owner - block producer owner account
         * @param quantity - reward quantity in EOS
         *
         * ### example
         *
         * ```json
         * [
         *   {
         *     "owner": "alice",
         *     "quantity": "8.800 EOS"
         *   }
         * ]
         * ```
         */
        struct [[eosio::table("rewards")]] rewards_row {
            name                owner;
            asset               quantity;

            uint64_t primary_key() const { return owner.value; }
        };
        typedef eosio::multi_index< "rewards"_n, rewards_row > rewards_table;

        /**
         * Claim rewards for a block producer.
         *
         * @param owner - block producer owner account
         */
        [[eosio::action]]
        void claimrewards( const name owner);

        [[eosio::on_notify("flon.token::transfer")]]
        void on_transfer( const name from, const name to, const asset quantity, const string memo );

    private:
    };
} /// namespace eosio

#include <flon.bpay/flon.bpay.hpp>

namespace eosio {

void bpay::claimrewards( const name owner ) {
    require_auth( owner );

    rewards_table _rewards( get_self(), get_self().value );

    const auto& row = _rewards.get( owner.value, "no rewards to claim" );

    eosio::token::transfer_action transfer( "flon.token"_n, { get_self(), "active"_n });
    transfer.send( get_self(), owner, row.quantity, "producer block pay" );

    _rewards.erase(row);
}

void bpay::on_transfer( const name from, const name to, const asset quantity, const string memo ) {
    if (from == get_self() || to != get_self()) {
        return;
    }

    // ignore eosio system incoming transfers (caused by bpay income transfers eosio => flon.bpay => producer)
    if ( from == "flon"_n) return;

    symbol system_symbol = eosiosystem::system_contract::get_core_symbol(get_self());

    check( quantity.symbol == system_symbol, "only core token allowed" );

    rewards_table _rewards( get_self(), get_self().value );
    eosiosystem::producers_table _producers( "flon"_n, "flon"_n.value );

    eosiosystem::global_state_singleton _global("flon"_n, "flon"_n.value);
    check( _global.exists(), "global state does not exist");
    uint16_t producer_count = _global.get().last_producer_schedule_size;

    // get producer with the most votes
    // using `by_votes` secondary index
    auto idx = _producers.get_index<"prototalvote"_n>();
    auto prod = idx.begin();

    // get top n producers by vote, excluding inactive
    std::vector<name> top_producers;
    while (true) {
        if (prod == idx.end()) break;
        if (prod->is_active == false) continue;

        top_producers.push_back(prod->owner);

        if (top_producers.size() == producer_count) break;

        prod++;
    }

    asset reward = quantity / top_producers.size();

    // distribute rewards to top producers
    for (auto producer : top_producers) {
        auto row = _rewards.find( producer.value );
        if (row == _rewards.end()) {
            _rewards.emplace( get_self(), [&](auto& row) {
                row.owner = producer;
                row.quantity = reward;
            });
        } else {
            _rewards.modify(row, get_self(), [&](auto& row) {
                row.quantity += reward;
            });
        }
    }
}

} /// namespace eosio

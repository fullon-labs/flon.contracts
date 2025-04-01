#include <pubkey.token/pubkey.token.hpp>

#include <eosio/transaction.hpp>
#include<pubkey.token/flon.system.hpp>
#include<utils.hpp>
#include<math.hpp>
#include<string>
#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/crypto.hpp>
#include<pubkey_utils.hpp>
#ifdef ENABLE_CONTRACT_VERSION
#include <contract_version.hpp>
#endif//ENABLE_CONTRACT_VERSION

namespace flon {

#ifdef ENABLE_CONTRACT_VERSION
DEFINE_VERSION_CONTRACT_CLASS("pubkey.token", pubkey_token)
#endif//ENABLE_CONTRACT_VERSION

//memo: pubkey: ****
void pubkey_token::ontransfer( name from, name to, asset quantity, string memo ){
   if( to != _self ) return;
   if( from == _self ) return;
   check( get_first_receiver() == FLON_BANK,  "Only `flon.token` is supported" );
   check( quantity.symbol != FLON_SYMBOL,     "Only FLON tokens are supported" );

   public_key pubkey;
   str_to_pubkey(memo, pubkey);
   _on_pubkey_recv_token(pubkey, quantity);

}

void pubkey_token::_on_pubkey_recv_token(const public_key& pubkey, const asset& quantity) {
   auto pubkey_hash = eosio::sha256(reinterpret_cast<const char*>(&pubkey), sizeof(pubkey));
   auto idx = _tbl_pubkey_accts.get_index<"by.pubkey"_n>();
   auto itr = idx.find( pubkey_hash );

   if(itr == idx.end()) {
      _tbl_pubkey_accts.emplace(_self, [&](auto& row) {
         _gstate.last_idx++;
         row.id            = _gstate.last_idx;
         row.pubkey        = pubkey;
         row.quantity      = quantity;
         row.last_recv_at  = current_time_point();
      });

   } else {
      _tbl_pubkey_accts.modify(*itr, same_payer, [&](auto& row) {
         row.quantity      += quantity;
         row.last_recv_at  = current_time_point();
      });
   }
}

void pubkey_token::newaccount(const name& miner, const eosio::public_key& pubkey, const name& acct, const eosio::signature& sig) {
   require_auth(miner);
   check( !is_account(acct),         "Account already exists" );

   // Check if the public key exists
   auto idx          = _tbl_pubkey_accts.get_index<"by.pubkey"_n>();
   auto pubkey_hash  = eosio::sha256(reinterpret_cast<const char*>(&pubkey), sizeof(pubkey));
   auto itr          = idx.find(pubkey_hash);
   check(itr         != idx.end(), "pubkey not found");
   check( itr->quantity >= _gstate.miner_fee, "insufficient proxy miner fees to pay" );

   auto digest       = hash_account(acct);
   assert_recover_key(digest, sig, pubkey);

   action(
      permission_level{ _self, "active"_n },
      FLON_SYSTEM, "newaccount"_n,
      std::make_tuple(_self, acct, pubkey, pubkey)
   ).send();

   TRANSFER(FLON_BANK, _self, miner, _gstate.miner_fee, "newaccount fee: " + acct.to_string());

   if( itr->quantity > _gstate.miner_fee ){
      TRANSFER(FLON_BANK, _self, acct, itr->quantity - _gstate.miner_fee,  "newaccount pubkey token collection");
   }

   idx.erase(itr);
}

void pubkey_token::move(const name& miner, const eosio::public_key& pubkey, const time_point& last_recv_at, const name& to_acct, const eosio::signature& sig) {
   check( is_account(to_acct),       "Account does not exist");
   check( pubkey != public_key(),    "Invalid public key");
   check( sig != signature(),        "Invalid signature");

   auto idx = _tbl_pubkey_accts.get_index<"by.pubkey"_n>();
   auto pubkey_hash = eosio::sha256(reinterpret_cast<const char*>(&pubkey), sizeof(pubkey));
   auto itr = idx.find(pubkey_hash);
   check( itr != idx.end(), "pubkey not found" );
   check( itr->quantity >= _gstate.miner_fee, "insufficient proxy miner fees to pay" );

   auto digest = hash_timepoint( last_recv_at );
   assert_recover_key(digest, sig, pubkey);

   check(itr->last_recv_at == last_recv_at, "Invalid last transfer time");

   TRANSFER(FLON_BANK, _self, miner, _gstate.miner_fee, "move fee: " + to_acct.to_string());
   if( itr->quantity > _gstate.miner_fee ){
      TRANSFER(FLON_BANK, _self, to_acct, itr->quantity - _gstate.miner_fee, "move pubkey tokens to account");
   }

   idx.erase(itr);
}


} // namespace flon


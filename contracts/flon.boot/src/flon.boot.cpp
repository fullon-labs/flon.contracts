#include <flon.boot/flon.boot.hpp>
#include <eosio/privileged.hpp>

#ifdef ENABLE_CONTRACT_VERSION
#include <contract_version.hpp>
#endif//ENABLE_CONTRACT_VERSION

namespace eosioboot {

#ifdef ENABLE_CONTRACT_VERSION
DEFINE_VERSION_CONTRACT_CLASS("flon.boot", boot)
#endif//ENABLE_CONTRACT_VERSION

void boot::onerror( ignore<uint128_t>, ignore<std::vector<char>> ) {
   check( false, "the onerror action cannot be called directly" );
}

void boot::activate( const eosio::checksum256& feature_digest ) {
   require_auth( get_self() );
   eosio::preactivate_feature( feature_digest );
}

void boot::reqactivated( const eosio::checksum256& feature_digest ) {
   check( eosio::is_feature_activated( feature_digest ), "protocol feature is not activated" );
}

}

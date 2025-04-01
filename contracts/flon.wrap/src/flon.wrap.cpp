#include <flon.wrap/flon.wrap.hpp>

#ifdef ENABLE_CONTRACT_VERSION
#include <contract_version.hpp>
#endif//ENABLE_CONTRACT_VERSION

namespace eosio {

#ifdef ENABLE_CONTRACT_VERSION
DEFINE_VERSION_CONTRACT_CLASS("flon.wrap", wrap)
#endif//ENABLE_CONTRACT_VERSION

void wrap::exec( ignore<name>, ignore<transaction> ) {
   require_auth( get_self() );

   name executer;
   _ds >> executer;

   require_auth( executer );

   transaction_header trx_header;
   std::vector<action> context_free_actions;
   std::vector<action> actions;
   _ds >> trx_header;
   _ds >> context_free_actions;
   check( context_free_actions.empty(), "not allowed to `exec` a transaction with context-free actions" );
   _ds >> actions;

   for (const auto& act : actions) {
      act.send();
   }
}

} /// namespace eosio

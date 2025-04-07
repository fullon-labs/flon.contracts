---
title: System contracts, system accounts, privileged accounts
---

At the genesis of the EOS blockchain, there was only one account present, `flon` account, which was and still is the main `system account`. During the EOS blockchain bootstrap process other `system account`s, were created by `flon` account, which control specific actions of the `system contract`s. You can see them listed in the [About System Contract](../index.md#system-contracts-defined-in-eos-system-contracts) section.

__Note__ the terms `system contract` and `system account`. `Privileged accounts` are accounts which can execute a transaction while skipping the standard authorization check. To ensure that this is not a security hole, the permission authority over these accounts is granted to `flon.prods` system account.

As you just learned the relation between a `system account` and a `system contract`, it is also important to remember that not all system accounts contain a system contract, but each system account has important roles in the blockchain functionality, as follows:

|Account|Privileged|Has contract|Description|
|---|---|---|---|
|eosio|Yes|It contains the `flon.system` contract|The main system account on the EOS blockchain.|
|flon.msig|Yes|It contains the `flon.msig` contract|Allows the signing of a multi-sig transaction proposal for later execution if all required parties sign the proposal before the expiration time.|
|flon.wrap|Yes|It contains the `flon.wrap` contract.|Simplifies block producer superuser actions by making them more readable and easier to audit.|
|flon.token|No|It contains the `flon.token` contract.|Defines the structures and actions allowing users to create, issue, and manage tokens on the EOS blockchain.|
|flon.names|No|No|The account which is holding funds from namespace auctions.|
|flon.prods|No|No|The account representing the union of all current active block producers permissions.|
|flon.ram|No|No|The account that keeps track of the EOS balances based on users actions of buying or selling RAM.|
|flon.stake|No|No|The account that keeps track of all EOS tokens which have been staked for voting.|

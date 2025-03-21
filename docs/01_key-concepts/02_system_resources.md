---
title: System Resources
---

The EOS blockchain works with three system resources: CPU, NET and RAM. The EOS accounts need sufficient system resources to interact with the smart contracts deployed on the blockchain.

* [RAM Resource](./05_ram.md)
* [CPU Resource](./03_cpu.md)
* [NET Resource](./04_net.md)

To allocate RAM resources to an account you have to [purchase RAM](./05_ram.md#how-to-purchase-ram).

## Resource Cost Estimation

As a developer if you want to estimate how much CPU and NET a transaction requires for execution, you can employ one of the following methods:

* Use the `--dry-run` option for the `dune -- fucli push transaction` command.
* Use any tool that can pack a transaction and send it to the blockchain and specify the `--dry-run` option.
* Use the chain API endpoint [`compute_transaction`](https://github.com/fullon-labs/fullon/blob/main/plugins/chain_plugin/include/eosio/chain_plugin/chain_plugin.hpp#L557).

In all cases, when the transaction is processed, the blockchain node simulates the execution of the transaction and, as a consequence, the state of the blockchain is changed speculatively, which allows for the CPU and NET measurements to be done. However, the transaction is not sent to the blockchain and the caller receives the estimated CPU and NET costs in return.

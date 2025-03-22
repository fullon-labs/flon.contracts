---
title: How to create, issue and transfer a token
link_text: How to create, issue and transfer a token
---

## Step 1: Obtain Contract Source

Navigate to your contracts directory.

```sh
cd CONTRACTS_DIR
```

Pull the source

```sh
git clone https://github.com/fullon-labs/flon.contracts --branch release/3.1 --single-branch
```

```sh
cd eos-system-contracts/contracts/flon.token
```

## Step 2: Create Account for Contract

[[info]]
| You may have to unlock your wallet first!

```shell
fucli create account flon flon.token EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV
```

## Step 3: Compile the Contract

```shell
flon-cpp -I include -o flon.token.wasm src/flon.token.cpp --abigen
```

## Step 4: Deploy the Token Contract

```shell

fucli set contract flon.token CONTRACTS_DIR/eos-system-contracts/contracts/flon.token --abi flon.token.abi -p flon.token@active
```

Result should look similar to the one below:

```console
Reading WASM from ...
Publishing contract...
executed transaction: 69c68b1bd5d61a0cc146b11e89e11f02527f24e4b240731c4003ad1dc0c87c2c  9696 bytes  6290 us
#         flon <= flon::setcode               {"account":"flon.token","vmtype":0,"vmversion":0,"code":"0061736d0100000001aa011c60037f7e7f0060047f...
#         flon <= flon::setabi                {"account":"flon.token","abi":"0e656f73696f3a3a6162692f312e30000605636c6f73650002056f776e6572046e61...
warning: transaction executed locally, but may not be confirmed by the network yet         ]
```

## Step 5: Create the Token

```shell
fucli push action flon.token create '[ "flon", "1000000000.0000 NEWT"]' -p flon.token@active
```

Result should look similar to the one below:

```console
executed transaction: 0e49a421f6e75f4c5e09dd738a02d3f51bd18a0cf31894f68d335cd70d9c0e12  120 bytes  1000 cycles
#   flon.token <= flon.token::create          {"issuer":"flon","maximum_supply":"1000000000.0000 NEWT"}
```

An alternate approach uses named arguments:

```shell
fucli push action flon.token create '{"issuer":"flon", "maximum_supply":"1000000000.0000 NEWT"}' -p flon.token@active
```

Result should look similar to the one below:

```console
executed transaction: 0e49a421f6e75f4c5e09dd738a02d3f51bd18a0cf31894f68d335cd70d9c0e12  120 bytes  1000 cycles
#   flon.token <= flon.token::create          {"issuer":"flon","maximum_supply":"1000000000.0000 NEWT"}
```

This command created a new token `NEWT` with a precision of 4 decimals and a maximum supply of 1000000000.0000 NEWT.  To create this token requires the permission of the `flon.token` contract. For this reason, `-p flon.token@active` was passed to authorize the request.

## Step 6: Issue Tokens

The issuer can issue new tokens to the issuer account in our case `flon`.

```sh
fucli push action flon.token issue '[ "flon", "100.0000 NEWT", "memo" ]' -p flon@active
```

Result should look similar to the one below:

```console
executed transaction: a26b29d66044ad95edf0fc04bad3073e99718bc26d27f3c006589adedb717936  128 bytes  337 us
#   flon.token <= flon.token::issue           {"to":"flon","quantity":"100.0000 NEWT","memo":"memo"}
warning: transaction executed locally, but may not be confirmed by the network yet         ]
```

## Step 7: Transfer Tokens

Now that account `flon` has been issued tokens, transfer some of them to account `bob`.

```shell
fucli push action flon.token transfer '[ "flon", "bob", "25.0000 NEWT", "m" ]' -p flon@active
```

Result should look similar to the one below:

```console
executed transaction: 60d334850151cb95c35fe31ce2e8b536b51441c5fd4c3f2fea98edcc6d69f39d  128 bytes  497 us
#   flon.token <= flon.token::transfer        {"from":"flon","to":"bob","quantity":"25.0000 NEWT","memo":"m"}
#         flon <= flon.token::transfer        {"from":"flon","to":"bob","quantity":"25.0000 NEWT","memo":"m"}
#           bob <= flon.token::transfer        {"from":"flon","to":"bob","quantity":"25.0000 NEWT","memo":"m"}
warning: transaction executed locally, but may not be confirmed by the network yet         ]
```

Now check if "bob" got the tokens using [fucli get currency balance](https://github.com/fullon-labs/fullon/blob/main/docs/02_cleos/03_command-reference/get/currency-balance.md)

```shell
fucli get currency balance flon.token bob NEWT
```

Result:

```console
25.00 NEWT
```

Check "flon's" balance, notice that tokens were deducted from the account

```shell
fucli get currency balance flon.token flon NEWT
```

Result:

```console
75.00 NEWT
```

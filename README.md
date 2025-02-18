# EOS system contracts

EOS system contracts are a collection of contracts specifically designed for the EOS blockchain, which implements a lot of critical functionality that goes beyond what is provided by the base Antelope protocol, the protocol on which EOS blockchain is built on.

The Antelope protocol includes capabilities such as:

* an accounts and permissions system which enables a flexible permission system that allows authorization authority over specific actions in a transaction to be satisfied by the appropriate combination of signatures;
* a consensus algorithm to propose and finalize blocks by a set of active block producers that can be arbitrarily selected by privileged smart contracts running on the blockchain;
* a basic resource management system that tracks usage of CPU/NET and RAM per account and enforces limits based on per-account quotas that can be adjusted by privileged smart contracts.

However, the Antelope protocol itself does not immediately provide:

* a mechanism for multiple accounts to reach consensus on authorization of a proposed transaction on-chain before executing it;
* a consensus mechanism that goes beyond the consensus algorithm to determine how block producers are selected and to align incentives by providing appropriate rewards and punishments to block producers or the entities that get them into that position;
* more sophisticated resource management systems that create markets for users to acquire resource rights;
* or, even something as seemingly basic as the concept of tokens (whether fungible or non-fungible).

The system contracts in this repository provide all of the above and more by building higher-level features or abstractions on top of the primitive mechanisms provided by the Antelope protocol.

The collection of system contracts consists of the following individual contracts:

* [boot contract](contracts/flon.boot/include/flon.boot/flon.boot.hpp): A minimal contract that only serves the purpose of activating protocol features which enables other more sophisticated contracts to be deployed onto the blockchain. (Note: this contract must be deployed to the privileged `flon` account.)
* [bios contract](contracts/flon.bios/include/flon.bios/flon.bios.hpp): A simple alternative to the core contract which is suitable for test chains or perhaps centralized blockchains. (Note: this contract must be deployed to the privileged `flon` account.)
* [token contract](contracts/flon.token/include/flon.token/flon.token.hpp): A contract enabling fungible tokens.
* [core contract](contracts/flon.system/include/flon.system/flon.system.hpp): A monolithic contract that includes a variety of different functions which enhances a base Antelope blockchain for use as a public, decentralized blockchain in an opinionated way. (Note: This contract must be deployed to the privileged `flon` account. Additionally, this contract requires that the token contract is deployed to the `flon.token` account and has already been used to setup the core token.) The functions contained within this monolithic contract include (non-exhaustive):
   + Delegated Proof of Stake (DPoS) consensus mechanism for selecting and paying (via core token inflation) a set of block producers that are chosen through delegation of the staked core tokens.
   + Allocation of CPU/NET resources based on core tokens in which the core tokens are either staked for an indefinite allocation of some fraction of available CPU/NET resources, or they are paid as a fee in exchange for a time-limited allocation of CPU/NET resources via REX or via PowerUp.
   + An automated market maker enabling a market for RAM resources which allows users to buy or sell available RAM allocations.
   + An auction for bidding for premium account names.
* [multisig contract](contracts/flon.msig/include/flon.msig/flon.msig.hpp): A contract that enables proposing Antelope transactions on the blockchain, collecting authorization approvals for many accounts, and then executing the actions within the transaction after authorization requirements of the transaction have been reached. (Note: this contract must be deployed to a privileged account.)
* [wrap contract](contracts/flon.wrap/include/flon.wrap/flon.wrap.hpp): A contract that wraps around any Antelope transaction and allows for executing its actions without needing to satisfy the authorization requirements of the transaction. If used, the permissions of the account hosting this contract should be configured to only allow highly trusted parties (e.g. the operators of the blockchain) to have the ability to execute its actions. (Note: this contract must be deployed to a privileged account.)

## Repository organization

The `main` branch contains the latest state of development; do not use this for production. Refer to the [releases page](https://github.com/fullon-labs/flon.contracts/releases) for current information on releases, pre-releases, and obsolete releases as well as the corresponding tags for those releases.
## Supported Operating Systems

[CDT](https://github.com/fullon-labs/flon.cdt) is required to build contracts. Any operating systems supported by CDT is sufficient to build the system contracts.

To build and run the tests as well, [fullon](https://github.com/fullon-labs/fullon) is also required as a dependency, which may have its further restrictions on supported operating systems.

## Building

The build guide below will assume you are running Ubuntu 22.04. However, as mentioned above, other operating systems may also be supported.

### Build or install CDT dependency

The CDT dependency is required. This release of the system contracts requires at least version 4.1.0 of CDT, and nodeos version fullon v1.0.0 or later.

The easiest way to satisfy this dependency is to install CDT on your system through a package. Find the release of a compatible version of CDT from its [releases page](https://github.com/fullon-labs/flon.cdt/releases), download the package file appropriate for your OS from the attached assets, and install the package.

Alternatively, you can build CDT from source. Please refer to the guide in the [CDT README](https://github.com/fullon-labs/flon.cdt#building-from-source) for instructions on how to do this. If you choose to go with building CDT from source, please keep the path to the build directory in the shell environment variable `CDT_BUILD_PATH` for later use when building the system contracts.

### Optionally build fullon dependency

The fullon dependency is optional. It is only needed if you wish to also build the tests using the `BUILD_TESTS` CMake flag.

Unfortunately, it is not currently possible to satisfy the contract testing dependencies through the fullon packages made available from the [fullon releases page](https://github.com/fullon-labs/fullon/releases). So if you want to build the contract tests, you will first need to build fullon from source.

Please refer to the guide in the [fullon README](https://github.com/fullon-labs/fullon#building-from-source) for instructions on how to do this. If you choose to go with building fullon from source, please keep the path to the build directory in the shell environment variable `FULLON_BUILD_PATH` for later use when building the system contracts.

### Build system contracts

Beyond CDT and optionally fullon (if also building the tests), no additional dependencies are required to build the system contracts.

The instructions below assume you are building the system contracts with tests, have already built fullon from source, and have the CDT dependency installed on your system. For some other configurations, expand the hidden panels placed lower within this section.

For all configurations, you should first `cd` into the directory containing cloned system contracts repository.

Build system contracts with tests using fullon built from source and with installed CDT package:

```shell
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -Dfullon_DIR="${FULLON_BUILD_PATH}/lib/cmake/fullon" ..
make -j $(nproc)
```

**Note:** `CMAKE_BUILD_TYPE` has no impact on the WASM files generated for the contracts. It only impacts how the test binaries are built. Use `-DCMAKE_BUILD_TYPE=Debug` if you want to create test binaries that you can debug.

<details>
<summary>Build system contracts with tests using fullon and CDT both built from source</summary>

```shell
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -Dcdt_DIR="${CDT_BUILD_PATH}/lib/cmake/cdt" -Dfullon_DIR="${FULLON_BUILD_PATH}/lib/cmake/fullon" ..
make -j $(nproc)
```

</details>

<details>
<summary>Build system contracts without tests and with CDT build from source</summary>

```shell
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF -Dcdt_DIR="${CDT_BUILD_PATH}/lib/cmake/cdt" ..
make -j $(nproc)
```

</details>

#### Supported CMake options

The following is a list of custom CMake options supported in building the system contracts (default values are shown below):

```text
-DBUILD_TESTS=OFF                       Do not build the tests

-DSYSTEM_CONFIGURABLE_WASM_LIMITS=ON    Enable use of the CONFIGURABLE_WASM_LIMITS
                                        protocol feature

-DSYSTEM_BLOCKCHAIN_PARAMETERS=ON       Enable use of the BLOCKCHAIN_PARAMETERS
                                        protocol feature
```

### Running tests

Assuming you built with `BUILD_TESTS=ON`, you can run the tests.

```shell
cd build/tests
ctest -j $(nproc)
```

## License

[MIT](LICENSE)

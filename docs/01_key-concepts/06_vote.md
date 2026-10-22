---
title: Voting on EOS blockchain
---

The EOS blockchain is kept alive by nodes which are interconnected between each other, communicating with each other via peer to peer protocols. Some of these nodes are elected, via a voting process, by the token holders to be producer nodes. They produce blocks, validate them and reach consensus on what transactions are allowed in each block, their order, and what blocks are finalized and stored forever in the blockchain state. This way the governance, the mechanism by which collective decisions are made, of the blockchain is achieved through the 21 active block producers which are appointed by token holders' votes. It is the 21 active block producers which continuously advancing the blockchain by creating blocks, and securing them by validating them, and reaching consensus. Consensus is reached when 2/3+1 active block producers agree on validity of a block, that is all transactions contained in it and their order. The 21 producers is the default value however it can be configured to be higher or smaller to meet each business case requirements.

The block producers are elected through the voting process. Each FLON token holder that wants to vote for one or up to 30 block producers must lock tokens for voting. One locked token creates one aggregate unit of voting power. When an account selects multiple producers, its aggregate power is divided equally among them rather than copied to every producer. Integer division rounds down and leaves the remainder unallocated until the account changes its stake or selections.

Changing the selected producers, adding vote stake, or subtracting vote stake first settles the voter's accrued rewards under the old allocation. The contract then removes the old per-producer weights and applies the newly normalized weights to every retained, added, and removed producer.

## Producer scheduling and epoch start

The system account initializes producer scheduling and base block-production rewards with `setprodcfg(schedule_start, reward_epoch_start, initial_block_reward)`:

- `schedule_start` is when vote-ranked producer schedules may become active.
- `reward_epoch_start` anchors epoch zero for performance measurement and starts the base block-production reward. It must be equal to or later than `schedule_start`.
- Base issuance is not configurable: the protocol fixes a 500,000,000 FLON mining budget and halves the scheduled era budget every 315,360,000 reward-eligible blocks. The implied first-era theoretical rate is approximately `0.7927447996 FLON` per eligible block.
- `initial_block_reward` is deprecated and retained only for deployed ABI compatibility; it must be `0 FLON` and is ignored by reward calculation.

The contract distributes each era's integer budget deterministically across its eligible blocks, including smallest-unit remainders, and enforces the 500,000,000 FLON cap on every reward accrual. Halving uses eligible rewarded-block count rather than wall-clock time, so chain downtime does not consume the mining schedule.
- Both timestamps must be in the future when first configured and become immutable once their respective processes have started.

This base block-production reward is distinct from the additional gas-funded FLON reward pool. Gas reward allocation is configured separately with `setgasrwcfg`, and eligible recipients claim accumulated rewards. The former `cfgelection` action has been removed in favor of the more explicit action and parameter names above.

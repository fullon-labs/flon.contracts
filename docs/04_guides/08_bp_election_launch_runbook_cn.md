---
title: BP 投票竞选上线与运行操作手册
---

# BP 投票竞选上线与运行操作手册

> 版本提示：本文的 1 小时 Epoch 参数适用于包含追加式
> `rewardplans` 计划表和 `setrwdcfg` action 的目标版本。已有链必须在未来的
> Epoch 边界追加新计划；修改前的 Epoch 继续使用当时生效的参数，修改后的
> Epoch 使用新参数，主网 ABI 不采用 `v2` 表名。

本文是 FullOn Block Producer（BP）投票竞选从测试链验证、主网上线到日常运行的操作手册。它覆盖链下节点准备、链上合约和参数启用，以及 Candidate、Active BP、Standby BP 和 Voter 的实际操作流程。

本文命令以 `fucli` 为例。执行前必须把 URL、账户、公钥、时间和数量替换成目标网络的真实值，并先在测试链完整演练。生产环境不得把私钥写入脚本、镜像、Git 仓库或命令历史。

## 1. 角色和奖励类型

| 角色 | 如何产生 | 主要职责 | 可领取奖励 |
| --- | --- | --- | --- |
| Candidate BP | 完成自有质押并注册 | 公开资料、运行同步节点、接受投票 | 未进入 Active/Standby 时无节点服务奖励 |
| Active BP | 合格 Candidate 中按有效票权排名进入当前法定席位 | 签名和生产区块、保持可用率 | 基础挖矿奖励、GAS Active 奖励；可按公开比例分享基础奖励给 Voter |
| Standby BP | Active BP 之后排名最高的最多 30 个合格 Candidate | 保持完整节点同步和生产就绪，随时接替 | 按 Epoch Standby 快照获得 GAS Standby 奖励；不领取 Active BP 基础挖矿奖励 |
| Voter | 锁定 FLON 获得不可转让用途的 GLON，并选择 BP | 动态投票、监督 BP、参与 DAO 治理 | BP 按 `reward_shared_ratio` 分享的基础挖矿奖励 |
| DAO / `flon` 系统权限 | 治理执行 | 设置安全参数、处理全网事故、逐步扩大 Active BP 法定数量 | 不直接领取上述奖励 |

Active 和 Standby 是协议根据快照自动产生的运行状态，不是 Candidate 自己选择的注册类型。当前目标为最多 21 个 Active BP 和 30 个 Standby BP；启动阶段 Active BP 法定数量可在 3 至 21 之间逐步扩大。

### 1.1 Active 与 Standby 奖励边界

```text
基础挖矿奖励
└─ 每个区块的毛额 100% 记给该块的实际出块 BP
   └─ Epoch 封存时逐 BP 应用绩效系数
      ├─ 不合格部分不转给其他 BP
      └─ 合格部分按 reward_shared_ratio 拆为 BP 自留与 Voter 分享

GAS 节点奖励
└─ 按 GAS 会计 Epoch 建立独立的多节点奖励池
   ├─ Active 池：多个实际参与出块的 Active BP 按 produced 比例分配
   └─ Standby 池：按合格节点数相对 30 席缩放后等额分配

Standby BP
├─ 无固定基础挖矿奖励
├─ 按 Epoch Standby 快照领取小比例 GAS 候补服务奖励
└─ 主要价值是保持生产就绪，并在排名或 Active 资格变化时递补
```

这里的“100%”仅表示**单个区块基础奖励毛额的初始归属**，不表示 BP 运营方最终保留全部金额，也不表示每块奖励会在全体 Active BP 之间二次按比例切分。每个 BP 的 Epoch 毛额随后独立经过绩效判定，并可通过注册时公开的 `reward_shared_ratio` 把合格部分分享给其 Voter。Standby 不参与基础挖矿奖励，只参与独立的 GAS Standby 池。

默认 GAS 节点奖励池为 total GAS 对应 FLON 的 10%；其中 Active 池占 80%，Standby 池最多占 20%。因此 Standby 理论上最多对应 total GAS 的 2%，并且按 30 个目标席位归一化，空缺席位不会被现有 Standby 瓜分。

## 2. 上线总体顺序

```text
版本冻结与审计
  -> 全部节点升级
  -> 激活 PRODUCER_SLOT_STATS 和 GAS_FEE_TOTALS
  -> 测试链部署与端到端演练
  -> 主网创建/核对合约账户和权限
  -> 部署 voter.reward、gas.reward、flon.system
  -> 设置 BP 质押、Active quorum、epoch 和奖励参数
  -> Candidate 质押并注册
  -> Voter 锁仓并投票
  -> 到达 schedule_start，协议自动更新 Active schedule
  -> 每 1 小时封存基础奖励和 GAS 奖励会计数据
  -> BP、Standby、Voter 各自领取已结算奖励
```

任何一步验证失败，都不得继续主网下一阶段。

## 3. 阶段 A：链下上线准备

### 3.1 冻结发布物

上线负责人记录并由至少两人复核：

- FLON Core、FLON CDT 和系统合约的 Git commit、tag、WASM/ABI SHA-256；
- `flon.system`、`voter.reward`、`gas.reward` 的部署文件；
- 测试链与主网 chain ID、RPC URL；
- `PRODUCER_SLOT_STATS` 和 `GAS_FEE_TOTALS` feature digest 及激活状态；
- Active BP 初始法定数量、最低自有质押、解质押等待期、启动时间；
- epoch 时长、Active 可用率门槛和 GAS 奖励比例；
- 回滚边界。合约数据迁移后不得假设可以简单回滚旧 WASM。

### 3.2 升级节点软件

先升级非出块 API、History 和 Standby 节点，再滚动升级 Active BP。每台机器至少验证：

```bash
curl -s "$RPC_URL/v1/chain/get_info"
```

检查 chain ID、head/LIB、节点版本、P2P 数量和同步延迟。BP 节点应使用独立签名权限、受保护的 producer API、稳定时钟、至少两个网络路径和链外监控。普通生产节点不建议同时承担高负载 History 查询。

### 3.3 验证 protocol feature

所有共识节点必须运行同时包含 `PRODUCER_SLOT_STATS` 和 `GAS_FEE_TOTALS` activation handler 的版本。激活后验证：

- feature 位于 activated protocol features，而不是仅位于 supported 列表；
- `get_producer_slot_stats` 已进入 intrinsic whitelist；
- `get_gas_fee_totals` 已进入 intrinsic whitelist；
- 测试合约能够调用 intrinsic；
- 重启和 replay 后状态仍一致。

不能只根据交易成功或节点进程存活判断 feature 已可用。

所有共识节点确认支持该 feature 后，由 `flon` 系统权限提交一次激活动作：

```bash
fucli -u "$RPC_URL" push action flon activate \
  '["<PRODUCER_SLOT_STATS_FEATURE_DIGEST>"]' -p flon@active

fucli -u "$RPC_URL" push action flon activate \
  '["<GAS_FEE_TOTALS_FEATURE_DIGEST>"]' -p flon@active
```

随后调用节点的 protocol-feature 查询接口，按 digest 核对其状态和激活区块。不得在仍有共识节点不支持该 feature 时提前激活；这是节点软件升级门禁，不是合约部署可以替代的步骤。

### 3.4 Candidate 链下资料要求

Candidate 在注册前准备：

- BP 账户和独立 block-signing key；
- HTTPS 网站，公开运营主体、地区、节点规格、监控状态、联系邮箱和 X 账号；
- 主网和测试网 P2P endpoint；
- 故障响应、密钥轮换、备份恢复和安全事件披露流程；
- 已同步到 LIB 附近且可随时启用签名的 BP/Standby 节点。

链上 `regproducer` 只保存 URL 和 location，因此网站必须作为公开资料入口，并长期保持可访问。

## 4. 阶段 B：测试链发布与验收

### 4.1 部署顺序

推荐顺序：

1. 部署或升级 `voter.reward`；
2. 部署或升级 `gas.reward`；
3. 最后部署或升级 `flon.system`；
4. 读取链上 code hash 和 ABI，与冻结发布物比对。

`flon.system` 会在 WASM 加载时解析其导入的 intrinsic。如果任一必需 feature 未激活，`setcode` 会以 `unresolveable` 拒绝，旧系统合约保持不变。不得通过修改 WASM 或绕过 whitelist 来规避该门禁。

新链需要初始化奖励合约；已经初始化的链不得重复执行 `init`。示例：

```bash
fucli -u "$RPC_URL" push action gas.reward init '["8,FLON"]' -p gas.reward@active
```

`voter.reward` 通常由 `flon.system::init` 初始化。升级既有链时应读取其 `global` singleton，确认已经初始化，而不是再次调用。

### 4.2 权限检查

至少核对以下 inline-action 能力，同时保留各账户 owner 恢复权限：

- `flon` 可以调用 `flon.token` 和 `voter.reward` 所需 inline action；
- `flon.vote` 和 `flon.stake` 允许系统合约按协议返还锁定资产；
- `flon.gas` 的 `active` 权限允许 `flon` 在 BP 主动领取时把已结算 GAS 奖励直接支付给受益 BP；
- `voter.reward` 和 `gas.reward` 可以把用户主动领取的 FLON 转出；
- GLON issuer 为 `flon`，GLON 的用户间普通转账受到协议限制。

不要直接照抄另一条链的 `updateauth`。先导出现有权限 JSON、计算变更后的 authority，再由多签审批。

### 4.3 设置启动参数

以下是当前推荐的测试示例，不代表 DAO 永久参数：

```bash
# 启动期 3 个 Active BP；以后可治理性扩大到 21。
fucli -u "$RPC_URL" push action flon setbpquorum '[3]' -p flon@active

# 强制排班检查自有质押，最低 500,000 FLON，退出等待 21 天。
fucli -u "$RPC_URL" push action flon setbpstake \
  '[true,"500000.00000000 FLON",1814400]' -p flon@active

# 第三个参数仅为旧 ABI 兼容，必须为 0 FLON，不参与奖励计算。
fucli -u "$RPC_URL" push action flon setprodcfg \
  '["<SCHEDULE_START_UTC>","<REWARD_EPOCH_START_UTC>","0.00000000 FLON"]' \
  -p flon@active

# 1 小时 epoch；95% 用于 Active 健康统计/全网事故判定；
# GAS 的 10% 进入节点池，其中 Active 80%、Standby 最多 20%。
fucli -u "$RPC_URL" push action flon setgasrwcfg \
  '[true,7200,9500,1000,8000]' -p flon@active
```

规则：

- `setbpquorum` 只能设置 3–21；
- `setbpstake` 的等待期必须为 1–365 天；
- `schedule_start` 必须在未来；`reward_epoch_start >= schedule_start`；
- 基础挖矿预算固定为 500,000,000 FLON，每 315,360,000 个奖励资格区块减半；
- GAS 参数启用后必须先用原参数关闭，才能更改配置，再重新启用；
- 主网建议至少预留 24–72 小时 Candidate 注册和 Voter 投票窗口。

### 4.4 测试链验收场景

必须至少走通：

1. 三个以上 Candidate 质押、注册并公开资料；
2. 多个 Voter `addvote -> voteproducer`，验证同一账户总票权不因多选而放大；
3. 到下一个 epoch 后，排名和 Active schedule 生效；
4. Active BP 正常出块，确认 Standby Epoch 快照与候补排名正确；
5. `onblock` 将基础奖励 Epoch 推进至 `SEALED` 后，BP/Voter 奖励可领取；
6. `onblock` 完成 GAS Epoch 的有界结算后，节点 GAS 奖励可领取；
7. `subvote` 立即收回并销毁等量 GLON，等待期后退回 FLON；
8. 模拟低可用率、suspended、冷却和 `resumeprod` probation；
9. 模拟全网事故，确认进入 incident hold，不能自动误罚多数 BP；
10. 节点重启、合约升级和 replay 后统计一致。

通过后冻结主网上线时间和参数，不在主网上临时试错。

## 5. 阶段 C：主网启动

主网重复测试链已验证的发布顺序和参数动作，并增加以下门禁：

- 至少两名发布人独立核对 chain ID、账户、WASM hash、ABI hash 和签名权限；
- 先确认 500,000,000 FLON 挖矿储备由 `flon` 系统账户实际持有。当前基础奖励是记账后由 `flon` 转账，不是 claim 时临时增发；
- 确认 `flon.gas` 有足够、且账务上可用于节点奖励的 FLON；
- 所有初始 Candidate 已同步并完成 block-signing 演练；
- schedule 切换窗口安排人工值守，不与 Core 升级、网络迁移同时进行。

启动后每分钟观察 proposed/active schedule、head/LIB 延迟、漏块率和 P2P。连续两个 epoch 正常后，再考虑把 Active quorum 从 3 逐步扩大到 5、7、11、15、21。每次扩大前都要确认新增 Candidate 已达到生产要求。

### 5.1 2026-10-23 主网合约升级记录

本次升级使用合约 commit `2cf0c11` 和 FLON CDT 0.9.0 在 xch 构建。发布物及链上验证结果：

| 账户 / Feature | 交易 ID | 结果 |
| --- | --- | --- |
| 创建 `gas.reward` | `03354b5797b52811f16838648fce5988e7883e597351e1ec5cd8a46567e727e8` | 账户创建并预置 GAS/流动 FLON |
| `gas.reward@active` 加入 `flon.code` | `096963c204e6f71a56746933756377f0921c96ebc326f1aeaa16969c99b63458` | 用户主动领奖所需 inline transfer 权限就绪 |
| 升级 `voter.reward` | `e1e54b6786e3030fa3a9f27c675c0c3353a1d3188f69a3b120696f58e4f16134` | code hash `6ab1945de244ade626525b62d8fe653d0dad16241fb83eafbeae9eebc27ddd5c` |
| 部署 `gas.reward` | `9dc44763693d51838bf28b38dbbd568361f985f59a75ff060d2f932d814904a0` | code hash `d3ebccbedbd61d498e6d8aee0495d14fd7c72729ac77d9c38881f49737ec7363` |
| 激活 `GAS_FEE_TOTALS` | `01a7138d1d9e3c99698a2e2c36b2c88f31895f53ed8ec3f2e063ecb8d20c31d4` | feature digest `a1a2ecbfda11c2ba64879d0ebccbf61cfd63523eb69d80bed7f3133444b9bdb9`，激活区块 `14877691` |
| 升级 `flon.system` | `42a9548816dc2f3fde8c4bf13741afee6ae5f2615eafb9d85822e7f7243a4fdf` | code hash `5413a08e3eb1a70c655bbb4d399da51e12ce7c5ce5a268823c9e51b65fcdfec7` |
| 初始化 `gas.reward` | `96e84e1ff24166a4c495a941a4e777ceef86b9e878ae935fbb491c41453fb835` | core symbol 为 `8,FLON` |

以上交易均已进入不可逆区块。升级后 ABI 中已移除 `standbyproof`，`setgasrwcfg` 为五参数接口，并已发布 `resumeprod`。

**合约部署不等于竞选启用。** 本次升级后 `gasrewcfg` 仍为空，且未代替 DAO 设置 `setbpquorum`、`setbpstake`、`setprodcfg` 和 `setgasrwcfg`。这些启动参数必须在候选节点就绪、时间窗口和治理决议明确后另行提交。

## 6. Candidate BP 操作流程

### 6.1 自有质押

```bash
fucli -u "$RPC_URL" push action flon stakebp \
  '["<BP_ACCOUNT>","500000.00000000 FLON"]' -p <BP_ACCOUNT>@active
```

质押 FLON 转入 `flon.stake`，记录在 `flon::bpstakes`。最低金额以 `bpstakecfg` 链上值为准。

`bpstakes` 使用 `generation` 标识每轮自质押生命周期，并使用 `committed` 标识该轮保证金是否已经用于 BP 注册。BP 自质押不会生成 GLON，也不计入 DAO 治理票权。

### 6.2 注册 Candidate

单签 block key：

```bash
fucli -u "$RPC_URL" push action flon regproducer \
  '["<BP_ACCOUNT>","<BLOCK_SIGNING_PUBLIC_KEY>","https://bp.example",<ISO_LOCATION>,<VOTER_SHARE_BP>]' \
  -p <BP_ACCOUNT>@active
```

`VOTER_SHARE_BP` 使用 basis points：`0` 为不分享，`1000` 为 10%，`10000` 为 100%。多签 block authority 使用 `regproducer2`。候选人列表必须在注册前明确披露分享比例；重新注册修改比例只影响后续记录的 epoch，不追溯改变已产生奖励。

### 6.3 保持候选资格

- 自有质押不得低于链上最低值；
- 网站、X、联系方式和 endpoint 持续有效；
- 节点保持同步，block key 与链上 authority 一致；
- 避免把高负载公开查询与生产签名放在同一实例；
- 监控 `producers`、`bpstatus`、`bpstats`、`epochstats` 和当前 schedule。

Candidate 按有效票权排序。进入 quorum 即成为 Active BP；紧随其后的最多 30 个合格 Candidate 成为 Standby BP。

### 6.4 退出和取回质押

先退出竞选，再发起全部或部分解质押：

```bash
fucli -u "$RPC_URL" push action flon unregprod '["<BP_ACCOUNT>"]' -p <BP_ACCOUNT>@active
fucli -u "$RPC_URL" push action flon unstakebp \
  '["<BP_ACCOUNT>","<QUANTITY> FLON"]' -p <BP_ACCOUNT>@active
```

从未用于 BP 注册的当前 `generation`（`committed=false`）可通过 `unstakebp` 立即赎回；一旦该轮自质押完成过 `regproducer` 或 `regproducer2`，即永久标记为 `committed=true`，即使随后退出竞选，该轮退款仍执行链上等待期。注册状态下只能取出超过最低门槛的部分，并且同一账户一次只能存在一笔待退自有质押。

待退款尚未领取时，`stakebp` 会拒绝开启新一轮质押，避免新旧生命周期混合。用户可等待期结束后领取：

```bash
fucli -u "$RPC_URL" push action flon claimbpstake '["<BP_ACCOUNT>"]' -p <BP_ACCOUNT>@active
```

也可以在领取前取消该笔赎回，将资金恢复到原 `generation`（仍保持 `committed=true`）：

```bash
fucli -u "$RPC_URL" push action flon cancelbpref '["<BP_ACCOUNT>"]' -p <BP_ACCOUNT>@active
```

当已提交生命周期的退款全部领取完成后，再次 `stakebp` 会创建新的 `generation`；新一轮在注册前仍可立即赎回。

## 7. Active BP 运行和领奖

### 7.1 生产要求

Active BP 必须运行 producer/signature 组件并按 schedule 出块。每产生一个区块，该块的基础奖励毛额只累计到实际出块 BP 的 `epochreward`，不会在其他 Active BP 之间分摊；Epoch 结束后再根据该 BP 的绩效和 `reward_shared_ratio` 确定 BP 自留、Voter 分享及不合格扣减金额：

| Epoch 可用率 | 状态/奖励结果 |
| --- | --- |
| `>= 99%` | qualified，100% 绩效系数 |
| `>= 95%` 且 `< 99%` | degraded，按 `produced / expected` 的实际比例结算 |
| `>= 90%` 且 `< 95%` | warning，基础挖矿奖励系数为 0，并累计 warning |
| `< 90%` | suspended，当期奖励为零；Standby 可递补 |

全网超过三分之一 BP 同时失败时会优先判定 network incident，避免把系统性故障误判为单节点作弊。

### 7.2 基础挖矿奖励领取

`onblock` 自动封存 epoch 后，BP 自留部分进入 `flon::producers.unclaimed_rewards`。BP 本人或第三方可触发统一领取：

```bash
fucli -u "$RPC_URL" push action flon claimrewards \
  '["<CALLER>","<BP_ACCOUNT>",24]' -p <CALLER>@active
```

未领取奖励会继续累计，不会因错过一个 epoch 丢失。

### 7.3 GAS Active 奖励领取

GAS epoch 由 `onblock` 自动完成有界内部结算。BP 使用上一节同一个 `flon::claimrewards(caller, producer, max_steps)` 同时拉取基础和 GAS 奖励，不再要求用户直接调用 `gas.reward`。

GAS Active 池由当前 schedule 中实际出过块的 BP 参与，严格按实际出块数比例分配，没有等额基础服务奖。`active_min_bp` 用于健康状态和全网事故判定；它不会把仍有实际出块、但低于该门槛的 Active BP 从 GAS 分配计算中直接删除。

### 7.4 停权恢复

`bpstatus` 显示 suspended 后，节点先修复并保持同步。达到 `suspended_until_epoch` 后由 BP 自己签名：

```bash
fucli -u "$RPC_URL" push action flon resumeprod \
  '["<BP_ACCOUNT>"]' -p <BP_ACCOUNT>@active
```

恢复后进入 probation，不会因冷却到期自动出块。试运行期间连续漏掉 12 个被分配 slot 会再次停权；重复停权冷却期按 1、2、4、8 个 epoch 递增。

## 8. Standby BP 运行和领奖

Standby 节点应保持完整同步和可立即接替状态，但不签名生产当前 schedule 的区块。原 `standbyproof` 仅能证明账户可签名并取得公共 RPC 数据，不能证明候选人运行了生产就绪的完整节点，因此已删除。

当前版本不对 Standby 声称在线能力作虚假判断。注册有效、自质押达标、未处于 `suspended` 状态且进入候补排名的账户，才可写入该 GAS epoch 的 `gasnodestats` Standby 快照，并获得该 epoch 的 Standby 奖励资格。该资格表示候补席位身份，不构成节点在线、独立同步或可随时出块的证明。未来应以共识可验证的真实测试 slot 或等效机制替代。

结算完成后使用统一入口领取：

```bash
fucli -u "$RPC_URL" push action flon claimrewards \
  '["<CALLER>","<BP_ACCOUNT>",24]' -p <CALLER>@active
```

Standby 池按最多 30 席归一化。未满 30 个合格 Standby 时，空缺份额不会被少数 Standby 瓜分。

## 9. Voter 质押、投票、改票、撤票和领奖

### 9.1 锁定 FLON 并获得 GLON

```bash
fucli -u "$RPC_URL" push action flon addvote \
  '["<VOTER>","<QUANTITY> FLON"]' -p <VOTER>@active
```

效果：

1. FLON 转入 `flon.vote`；
2. 用户收到等量 GLON 作为治理权凭证；
3. 新增票权排队到下一奖励 epoch 生效；
4. GLON 不应在用户之间自由转账，不能与锁定 FLON 的赎回权脱钩。

只执行 `addvote` 但未选择 Candidate，不会产生 BP 排名分配。

### 9.2 选择 BP

```bash
fucli -u "$RPC_URL" push action flon voteproducer \
  '["<VOTER>",["bpone","bptwo","bpthree"]]' -p <VOTER>@active
```

- 最多选择 30 个 Candidate；
- 账户名必须去重并按升序排列；
- 总票权在所选 BP 之间平均分配，不能把同一份票复制放大；
- 整数除法余数不分配；
- epoch N 内提交的变化从 N+1 开始影响排名和对应奖励快照。

改票使用同一个 `voteproducer` action 提交完整的新列表。传空列表表示不再支持任何 BP，但不会自动赎回锁定 FLON。

### 9.3 增加或减少票权

增加票权再次调用 `addvote`。减少票权：

```bash
fucli -u "$RPC_URL" push action flon subvote \
  '["<VOTER>","<QUANTITY> FLON"]' -p <VOTER>@active
```

`subvote` 会立即把等量 GLON 转回系统并销毁，票权变化排队到下一 epoch；FLON 进入待退款状态。当前实现同一账户只能有一笔待退款。

等待期成熟后可由 owner 或任何第三方触发，资金只能退回 owner：

```bash
fucli -u "$RPC_URL" push action flon voterefund '["<VOTER>"]'
```

当前合约等待期为 180 秒；主网前应确认该参数是否符合最终治理决定。

### 9.4 领取 Voter 奖励

BP 在注册时设置的 `reward_shared_ratio` 决定其基础奖励中分享给 Voter 的比例。`onblock` 的有界封存状态机把该份额转入 `voter.reward`，并按奖励产生 epoch 的历史有效票权记账。Voter 本人或第三方可触发领取：

```bash
fucli -u "$RPC_URL" push action voter.reward claimrewards \
  '["<CALLER>","<VOTER>",24]' -p <CALLER>@active
```

领取时间不改变归属；延后领取会累计。Voter 不通过 `gas.reward` 领取投票分享奖励。

## 10. `onblock` 自动封存与结算流程

当前版本不再公开 `closeepoch`、`beginsettle`、`settlenext`、`finalizeepoch`，也不依赖 Keeper 推进协议状态。

每个 1 小时 Epoch 到期后，`onblock` 以严格有界的步骤推进：

```text
OPEN
  -> ENDING：封存本 Epoch 的出块与 GAS 统计
  -> APPLYING_DELTAS：逐块处理绩效、奖励行和历史票权边界
  -> SEALED：该 Epoch 可领取，并开始处理下一个已结束 Epoch
```

基础奖励与 GAS 奖励共用同一次 `get_producer_slot_stats` 采样。每个区块只推进固定数量的行；即使积压多个 Epoch，也不会在单个区块内做无界全表扫描。链下服务只负责只读监控和告警，不发送结算交易。

应监控：

- `rewardseal.phase`、`stage`、`cursor` 是否持续推进；
- `rewardepoch.next_epoch_to_close` 是否最终跨过已结束的 Epoch；
- `gasautostate.next_epoch` 和 `gassettles` 是否持续推进；
- 长时间不推进时的链头、LIB、节点日志和精确合约错误。

错过边界不会丢失奖励，也不会把 Epoch 拉长；结束时间仍由链上 slot 确定。恢复出块后，后续 `onblock` 会按顺序继续处理积压状态。不得通过外部交易跳过 Epoch 或重写游标。

全网事故判断仍可进入 incident 状态。DAO/系统权限完成链下复核后，可调用：

```bash
fucli -u "$RPC_URL" push action flon resolveincdt \
  '[<GAS_EPOCH_ID>,<true|false>]' -p flon@active
```

其中 `true` 表示继续该 Epoch 的 GAS 分配，`false` 表示该 Epoch 不发放。

## 11. 常用链上检查

```bash
# Candidate 排名、Active 标志、票数、分享比例和待领基础奖励
fucli -u "$RPC_URL" get table flon flon producers

# Voter 当前已生效票权与 BP 列表
fucli -u "$RPC_URL" get table flon flon voters

# 下一 epoch 待生效票权
fucli -u "$RPC_URL" get table flon flon pendingvote

# BP 自有质押、待退款和配置
fucli -u "$RPC_URL" get table flon flon bpstakes
fucli -u "$RPC_URL" get table flon flon bpstkrefund
fucli -u "$RPC_URL" get table flon flon bpstakecfg

# BP 停权/恢复状态与基础 epoch 进度
fucli -u "$RPC_URL" get table flon flon bpstatus
fucli -u "$RPC_URL" get table flon flon rewardepoch

# 指定 epoch 的出块统计
fucli -u "$RPC_URL" get table flon <BASE_EPOCH_ID> bpstats

# GAS 配置、角色、统计和结算
fucli -u "$RPC_URL" get table flon flon gasrewcfg
fucli -u "$RPC_URL" get table flon flon gasepochcfg
fucli -u "$RPC_URL" get table flon <GAS_EPOCH_ID> gasnodestats
fucli -u "$RPC_URL" get table flon flon gassettles

# 节点按 Epoch 封存的基础与 GAS 待领明细
fucli -u "$RPC_URL" get table flon <PRODUCER> prodclaims
fucli -u "$RPC_URL" get table flon <PRODUCER> gasclaims

# Voter 奖励状态
fucli -u "$RPC_URL" get table voter.reward voter.reward voters
```

## 12. 上线完成标准

只有以下条件全部满足，才能宣布投票竞选正式上线：

- 所有共识节点版本一致，feature 激活和 replay 均验证通过；
- 三个合约 code/ABI hash 与冻结发布物一致；
- 权限、挖矿储备、`flon.gas` 奖励余额和 GLON issuer 已复核；
- Active schedule 达到治理设定 quorum，LIB 持续推进；
- 至少一个完整基础 epoch 和 GAS epoch 已结算；
- Active BP、Standby BP、Voter 三类 `claimrewards` 均实测到账；
- addvote、voteproducer、subvote、voterefund 全流程完成；
- suspended、resumeprod、probation 和 incident hold 已在测试链演练；
- 只读监控、告警、值班和事故沟通渠道均已运行。

## 13. 不应执行的操作

- 不要把 BP 称为 Supernode；使用 Block Producer（BP）、Active BP、Standby BP；
- 不要手工指定某账户为 Standby，角色应由 epoch 快照和排名产生；
- 不要让 `initial_block_reward` 接受非零值；基础发行曲线是协议固定值；
- 不要重新加入外部 `closeepoch/beginsettle/settlenext/finalizeepoch` 正确性依赖；
- 不要通过外部交易篡改自动封存的 phase/cursor；
- 新奖励统一使用 `flon::claimrewards`；`gas.reward::claimrewards` 只处理升级前已存在的遗留余额，Voter 使用 `voter.reward::claimrewards`；
- 不要把测试链 chain ID、账户或 endpoint 复制到主网脚本；
- 不要以 HTTP 200、容器运行中或交易已广播作为上线成功证明，必须检查链上最终状态和实际到账。

---
title: BP 投票竞选上线与运行操作手册
---

# BP 投票竞选上线与运行操作手册

本文是 FullOn Block Producer（BP）投票竞选从测试链验证、主网上线到日常运行的操作手册。它覆盖链下节点准备、链上合约和参数启用，以及 Candidate、Active BP、Standby BP、Voter 和结算 Keeper 的实际操作流程。

本文命令以 `fucli` 为例。执行前必须把 URL、账户、公钥、时间和数量替换成目标网络的真实值，并先在测试链完整演练。生产环境不得把私钥写入脚本、镜像、Git 仓库或命令历史。

## 1. 角色和奖励类型

| 角色 | 如何产生 | 主要职责 | 可领取奖励 |
| --- | --- | --- | --- |
| Candidate BP | 完成自有质押并注册 | 公开资料、运行同步节点、接受投票 | 未进入 Active/Standby 时无节点服务奖励 |
| Active BP | 合格 Candidate 中按有效票权排名进入当前法定席位 | 签名和生产区块、保持可用率 | 基础挖矿奖励、GAS Active 奖励；可按公开比例分享基础奖励给 Voter |
| Standby BP | Active BP 之后排名最高的最多 30 个合格 Candidate | 保持节点同步、定期提交在线证明、随时接替 | GAS Standby 奖励；不领取 Active BP 基础挖矿奖励 |
| Voter | 锁定 FLON 获得不可转让用途的 GLON，并选择 BP | 动态投票、监督 BP、参与 DAO 治理 | BP 按 `reward_shared_ratio` 分享的基础挖矿奖励 |
| Keeper | 任意账户或自动化服务 | 关闭基础奖励 epoch、分批结算 GAS epoch | 无协议保证的 Keeper 奖励 |
| DAO / `flon` 系统权限 | 治理执行 | 设置安全参数、处理全网事故、逐步扩大 Active BP 法定数量 | 不直接领取上述奖励 |

Active 和 Standby 是协议根据快照自动产生的运行状态，不是 Candidate 自己选择的注册类型。当前目标为最多 21 个 Active BP 和 30 个 Standby BP；启动阶段 Active BP 法定数量可在 3 至 21 之间逐步扩大。

### 1.1 Active 与 Standby 奖励边界

```text
基础挖矿奖励
└─ 100% 分配给实际出块并满足绩效条件的 Active BP 类别
   └─ 按合格出块和绩效结果计算，不为 Standby 预留固定份额

Standby BP
├─ 无固定基础挖矿奖励
├─ 完成在线 proof 后可领取小比例 GAS 在线服务奖励
└─ 主要价值是保持生产就绪，并在排名或 Active 资格变化时递补
```

这里的“100%”是指基础挖矿奖励在**节点角色类别**之间全部归属于 Active BP，不表示 BP 运营方必然保留全部金额。每个 Active BP 仍可通过注册时公开的 `reward_shared_ratio`，把自己的部分基础奖励分享给其 Voter。Standby 不参与该基础挖矿奖励，只参与独立的 GAS Standby 池。

默认 GAS 节点奖励池为 total GAS 对应 FLON 的 10%；其中 Active 池占 80%，Standby 池最多占 20%。因此 Standby 理论上最多对应 total GAS 的 2%，并且按 30 个目标席位归一化，空缺席位不会被现有 Standby 瓜分。

## 2. 上线总体顺序

```text
版本冻结与审计
  -> 全部节点升级
  -> 激活 PRODUCER_SLOT_STATS
  -> 测试链部署与端到端演练
  -> 主网创建/核对合约账户和权限
  -> 部署 voter.reward、gas.reward、flon.system
  -> 设置 BP 质押、Active quorum、epoch 和奖励参数
  -> Candidate 质押并注册
  -> Voter 锁仓并投票
  -> 到达 schedule_start，协议自动更新 Active schedule
  -> 每 6 小时执行基础奖励和 GAS 奖励结算
  -> BP、Standby、Voter 各自领取已结算奖励
```

任何一步验证失败，都不得继续主网下一阶段。

## 3. 阶段 A：链下上线准备

### 3.1 冻结发布物

上线负责人记录并由至少两人复核：

- FLON Core、FLON CDT 和系统合约的 Git commit、tag、WASM/ABI SHA-256；
- `flon.system`、`voter.reward`、`gas.reward` 的部署文件；
- 测试链与主网 chain ID、RPC URL；
- `PRODUCER_SLOT_STATS` feature digest 和激活状态；
- Active BP 初始法定数量、最低自有质押、解质押等待期、启动时间；
- epoch 时长、可用率门槛、Standby proof 次数、GAS 奖励比例；
- 回滚边界。合约数据迁移后不得假设可以简单回滚旧 WASM。

### 3.2 升级节点软件

先升级非出块 API、History 和 Standby 节点，再滚动升级 Active BP。每台机器至少验证：

```bash
curl -s "$RPC_URL/v1/chain/get_info"
```

检查 chain ID、head/LIB、节点版本、P2P 数量和同步延迟。BP 节点应使用独立签名权限、受保护的 producer API、稳定时钟、至少两个网络路径和链外监控。普通生产节点不建议同时承担高负载 History 查询。

### 3.3 验证 protocol feature

所有共识节点必须运行包含 `PRODUCER_SLOT_STATS` activation handler 的版本。激活后验证：

- feature 位于 activated protocol features，而不是仅位于 supported 列表；
- `get_producer_slot_stats` 已进入 intrinsic whitelist；
- 测试合约能够调用 intrinsic；
- 重启和 replay 后状态仍一致。

不能只根据交易成功或节点进程存活判断 feature 已可用。

所有共识节点确认支持该 feature 后，由 `flon` 系统权限提交一次激活动作：

```bash
fucli -u "$RPC_URL" push action flon activate \
  '["<PRODUCER_SLOT_STATS_FEATURE_DIGEST>"]' -p flon@active
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

新链需要初始化奖励合约；已经初始化的链不得重复执行 `init`。示例：

```bash
fucli -u "$RPC_URL" push action gas.reward init '["8,FLON"]' -p gas.reward@active
```

`voter.reward` 通常由 `flon.system::init` 初始化。升级既有链时应读取其 `global` singleton，确认已经初始化，而不是再次调用。

### 4.2 权限检查

至少核对以下 inline-action 能力，同时保留各账户 owner 恢复权限：

- `flon` 可以调用 `flon.token`、`voter.reward` 和 `gas.reward` 所需 inline action；
- `flon.vote` 和 `flon.stake` 允许系统合约按协议返还锁定资产；
- `flon.gas` 允许系统合约把已结算 GAS 奖励转入 `gas.reward`；
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

# 6 小时 epoch；95% 用于 Active 健康统计/全网事故判定；Standby 每 epoch 12 次 proof；
# GAS 的 10% 进入节点池，其中 Active 80%、Standby 最多 20%。
fucli -u "$RPC_URL" push action flon setgasrwcfg \
  '[true,43200,9500,12,1000,8000]' -p flon@active
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
4. Active BP 正常出块，Standby 按间隔提交 12 次 proof；
5. `closeepoch` 后 BP/Voter 基础奖励可领取；
6. GAS epoch 完成 `beginsettle -> settlenext -> finalizeepoch` 后节点可领取；
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

## 6. Candidate BP 操作流程

### 6.1 自有质押

```bash
fucli -u "$RPC_URL" push action flon stakebp \
  '["<BP_ACCOUNT>","500000.00000000 FLON"]' -p <BP_ACCOUNT>@active
```

质押 FLON 转入 `flon.stake`，记录在 `flon::bpstakes`。最低金额以 `bpstakecfg` 链上值为准。

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

注册状态下只能取出超过最低门槛的部分，并且同一账户一次只能存在一笔待退自有质押。等待期结束后：

```bash
fucli -u "$RPC_URL" push action flon claimbpstake '["<BP_ACCOUNT>"]' -p <BP_ACCOUNT>@active
```

## 7. Active BP 运行和领奖

### 7.1 生产要求

Active BP 必须运行 producer/signature 组件并按 schedule 出块。基础奖励按实际合格出块累计，epoch 结束后才根据绩效确定最终可得金额：

| Epoch 可用率 | 状态/奖励结果 |
| --- | --- |
| `>= 99%` | qualified，100% 绩效系数 |
| `>= 95%` 且 `< 99%` | degraded，按 `produced / expected` 的实际比例结算 |
| `>= 90%` 且 `< 95%` | warning，基础挖矿奖励系数为 0，并累计 warning |
| `< 90%` | suspended，当期奖励为零；Standby 可递补 |

全网超过三分之一 BP 同时失败时会优先判定 network incident，避免把系统性故障误判为单节点作弊。

### 7.2 基础挖矿奖励领取

Keeper 关闭 epoch 后，BP 自留部分进入 `flon::producers.unclaimed_rewards`。BP 主动领取：

```bash
fucli -u "$RPC_URL" push action flon claimrewards \
  '["<BP_ACCOUNT>"]' -p <BP_ACCOUNT>@active
```

未领取奖励会继续累计，不会因错过一个 epoch 丢失。

### 7.3 GAS Active 奖励领取

GAS epoch 完成最终结算后，Active BP 到另一个合约领取：

```bash
fucli -u "$RPC_URL" push action gas.reward claimrewards \
  '["<BP_ACCOUNT>"]' -p <BP_ACCOUNT>@active
```

GAS Active 池由当前 schedule 中实际出过块的 BP 参与，严格按实际出块数比例分配，没有等额基础服务奖。`active_min_bp` 用于健康状态和全网事故判定；它不会把仍有实际出块、但低于该门槛的 Active BP 从 GAS 分配计算中直接删除。

### 7.4 停权恢复

`bpstatus` 显示 suspended 后，节点先修复并保持同步。达到 `suspended_until_epoch` 后由 BP 自己签名：

```bash
fucli -u "$RPC_URL" push action flon resumeprod \
  '["<BP_ACCOUNT>"]' -p <BP_ACCOUNT>@active
```

恢复后进入 probation，不会因冷却到期自动出块。试运行期间连续漏掉 12 个被分配 slot 会再次停权；重复停权冷却期按 1、2、4、8 个 epoch 递增。

## 8. Standby BP 运行和领奖

Standby 节点必须保持完整同步和可立即接替状态，但不签名生产当前 schedule 的区块。每个 GAS epoch 应均匀提交在线 proof；默认 6 小时 epoch、12 次 proof，最小间隔约 30 分钟。

先读取最新不可逆或足够新的 canonical block number/id，再提交：

```bash
fucli -u "$RPC_URL" push action flon standbyproof \
  '["<BP_ACCOUNT>",<GAS_EPOCH_ID>,<RECENT_BLOCK_NUM>,"<RECENT_BLOCK_ID>"]' \
  -p <BP_ACCOUNT>@active
```

限制：

- 账户必须在该 epoch 的 `gasnodestats` 快照中标记为 Standby；
- block 必须是 canonical，且距离当前高度不超过 240 块；
- 同一 block id 不能重复；proof 不能过于密集；
- 达到阈值后无需继续提交。

结算完成后领取：

```bash
fucli -u "$RPC_URL" push action gas.reward claimrewards \
  '["<BP_ACCOUNT>"]' -p <BP_ACCOUNT>@active
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

等待期成熟后可由任何 Keeper 触发，资金只能退回 owner：

```bash
fucli -u "$RPC_URL" push action flon voterefund '["<VOTER>"]'
```

当前合约等待期为 180 秒；主网前应确认该参数是否符合最终治理决定。

### 9.4 领取 Voter 奖励

BP 在注册时设置的 `reward_shared_ratio` 决定其基础奖励中分享给 Voter 的比例。`flon::closeepoch` 把该份额转入 `voter.reward`，并按奖励产生 epoch 的历史有效票权记账。Voter 主动领取：

```bash
fucli -u "$RPC_URL" push action voter.reward claimrewards \
  '["<VOTER>"]' -p <VOTER>@active
```

领取时间不改变归属；延后领取会累计。Voter 不通过 `gas.reward` 领取投票分享奖励。

## 10. Keeper：每 6 小时结算流程

建议基金会运行一个主 Keeper，并由至少两个独立 BP/社区 Keeper 监控和补单。所有 permissionless action 都必须设计成幂等检查后再发送，不能盲目重复交易。

### 10.1 基础挖矿和 Voter 分享奖励

基础 epoch 从 `reward_epoch_start` 起每 6 小时编号，必须严格顺序关闭：

```bash
fucli -u "$RPC_URL" push action flon closeepoch '[<BASE_EPOCH_ID>]'
```

该 action：

- 固化 BP 绩效状态和停权结果；
- 把 BP 自留部分记入 `producers.unclaimed_rewards`；
- 把 Voter 分享池转入 `voter.reward`；
- 应用下一 epoch 的票权变化；
- 推进 `rewardepoch.next_epoch_to_close`。

`closeepoch` 不由 `onblock` 自动执行。`onblock` 只记录逐块奖励和确定性出块统计；完整结算包含表扫描、绩效判定、inline transfer 和待生效票权更新，必须由链下 Keeper 发起 permissionless 交易，避免结算失败或 CPU 超限阻塞共识出块。

#### 10.1.1 日常执行频率和时间窗口

基础 epoch 固定为 6 小时。Keeper 不需要每块发送交易，推荐采用以下运行节奏：

| 项目 | 推荐值 | 说明 |
| --- | --- | --- |
| 状态检查频率 | 每 1 分钟；最长不超过 5 分钟 | 只读查询 `rewardepoch.next_epoch_to_close` 和链头时间，不满足关闭条件时不发交易 |
| 正常提交窗口 | epoch 边界后 1–5 分钟 | 给新区块和 RPC 留出传播时间，然后关闭刚结束的 epoch |
| 延迟告警 | 边界后 15 分钟仍未关闭 | 主 Keeper 告警，由备用 Keeper 复核并补单 |
| 严重告警 | 落后 1 个完整 epoch，即超过约 6 小时 | 立即按顺序追赶，并检查 Keeper、钱包、RPC 和合约执行日志 |
| 高可用配置 | 1 个主 Keeper，加至少 2 个独立监控/备用 Keeper | 备用 Keeper 必须先读链上状态，不能按本地定时器盲发 |

每轮执行前都必须读取：

```bash
fucli -u "$RPC_URL" get info
fucli -u "$RPC_URL" get table flon flon rewardepoch
```

只有同时满足以下条件才提交：

1. RPC chain ID 与目标网络配置完全一致；
2. 链头和 LIB 正常推进；
3. `next_epoch_to_close` 对应的 epoch 已结束；
4. Keeper 钱包和签名权限属于目标网络；
5. 当前没有另一笔相同 epoch 的待确认交易。

提交后至少等待交易进入不可逆区块，并重新读取 `rewardepoch`。只有观察到 `next_epoch_to_close` 增加 1，才能认为本次关闭完成。交易广播成功或 RPC 返回 HTTP 200 都不能单独作为完成依据。

#### 10.1.2 错过一次或多次的处理

错过关闭窗口不会销毁基础挖矿奖励或 Voter 分享奖励。各 epoch 的奖励数据仍保留在独立 scope 中，之后可以补做；但延迟期间会产生以下影响：

- BP 和 Voter 的奖励所有权尚未最终结算，暂时不能领取对应部分；
- `pendingvote` / `votedeltas` 尚未激活，新的票权排名和 Active schedule 会延迟；
- BP 绩效状态、停权和恢复判定延迟；
- 未关闭 epoch 的表数据继续占用链上 RAM；
- 监控和 DApp 展示的“待生效”状态会持续存在。

如果只错过一次，直接读取 `rewardepoch.next_epoch_to_close`，确认该 epoch 已结束后提交一次 `closeepoch` 即可。

如果错过多次，不能跳到最新 epoch，也不能并发提交多个 `closeepoch`。合约要求严格顺序关闭，安全追赶流程是：

1. 读取链上的 `next_epoch_to_close`，把它作为唯一可信的下一个 epoch ID；
2. 提交 `closeepoch(next_epoch_to_close)`；
3. 等待不可逆，并确认链上值恰好增加 1；
4. 重新读取状态；如果新的 `next_epoch_to_close` 也已经结束，继续下一轮；
5. 追到当前尚未结束的 epoch 后停止。

不要预先生成一串交易，也不要依赖本地缓存的 epoch ID。两名 Keeper 同时补单时，后到交易通常会因 `reward epochs must be closed sequentially` 被拒绝；这是安全拒绝，但会浪费交易资源并制造无效告警。

长时间积压通常不会把多个 epoch 合并成一笔超大结算：每次 action 仍只处理一个 epoch。因此可以顺序追赶，奖励不会因为跨过多个 6 小时窗口而丢失。不过，每个 epoch 本身仍可能包含较多 BP 绩效和奖励行；若单笔交易出现 CPU/NET 超限，必须保留精确错误和表状态，停止盲目重试，并评估是否需要通过合约升级把结算改为有界、可续跑的分批状态机。

#### 10.1.3 Keeper 自动化逻辑

推荐的 Keeper 循环是“查询—判断—提交—等待不可逆—复核”，而不是固定每 6 小时无条件发送：

```text
每 1 分钟执行一次只读检查
  ├─ 链未同步 / chain ID 不匹配：停止并告警
  ├─ next_epoch_to_close 尚未结束：本轮退出
  └─ 已结束：提交 closeepoch(next_epoch_to_close)
       ├─ 等待不可逆
       ├─ 验证 next_epoch_to_close == 原值 + 1
       └─ 如仍有积压，串行处理下一 epoch
```

基础 `closeepoch` 与 GAS `beginsettle/settlenext/finalizeepoch` 是两套独立编号和状态机。基础 epoch 追赶完成不等于 GAS epoch 已结算，Keeper 必须分别监控，严禁混用 epoch ID。

### 10.2 GAS 节点奖励

GAS epoch 使用绝对 block slot 除以 `epoch_slots` 的编号方式，与基础 epoch 编号不要混用。

```bash
# 1. 创建已结束 epoch 的结算记录
fucli -u "$RPC_URL" push action flon beginsettle '[<GAS_EPOCH_ID>]'

# 2. 第一阶段扫描资格；每次最多 10 行，重复直到 phase 从 1 变化
fucli -u "$RPC_URL" push action flon settlenext '[<GAS_EPOCH_ID>,10]'

# 3. 第二阶段分配并写入 gas.reward；继续重复直到 phase == 3
fucli -u "$RPC_URL" push action flon settlenext '[<GAS_EPOCH_ID>,10]'

# 4. 核对 allocated == pool 后最终确认
fucli -u "$RPC_URL" push action flon finalizeepoch '[<GAS_EPOCH_ID>]'
```

如果 `gassettles.phase == 4`，表示检测到全网事故，普通 Keeper 必须停止。DAO/系统权限完成链下事故复核后选择：

```bash
# proceed=true：继续资助和分配；false：该 epoch 不发放
fucli -u "$RPC_URL" push action flon resolveincdt \
  '[<GAS_EPOCH_ID>,<true|false>]' -p flon@active
```

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

# 节点 GAS 待领余额
fucli -u "$RPC_URL" get table gas.reward gas.reward noderewards

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
- Keeper、监控、告警、值班和事故沟通渠道均已运行。

## 13. 不应执行的操作

- 不要把 BP 称为 Supernode；使用 Block Producer（BP）、Active BP、Standby BP；
- 不要手工指定某账户为 Standby，角色应由 epoch 快照和排名产生；
- 不要让 `initial_block_reward` 接受非零值；基础发行曲线是协议固定值；
- 不要在 `onblock` 中做完整 epoch 结算；结算由 permissionless 交易触发；
- 不要在未核对 phase/cursor 的情况下并发发送多笔 `settlenext`；
- 不要把 `flon::claimrewards`、`gas.reward::claimrewards` 和 `voter.reward::claimrewards` 混用；
- 不要把测试链 chain ID、账户或 endpoint 复制到主网脚本；
- 不要以 HTTP 200、容器运行中或交易已广播作为上线成功证明，必须检查链上最终状态和实际到账。

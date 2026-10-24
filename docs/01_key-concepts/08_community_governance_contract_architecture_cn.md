# FLON 社区治理合约架构

## 1. 结论

社区治理首版新增两个合约：

1. `dao.gov`：提案、附议、快照投票、结果确认、Timelock 和受控执行。
2. `dao.treasury`：治理资金保管、预算预留、分期付款和执行记录。

不新增独立的投票质押合约、论坛合约或策略合约：

- 治理权继续使用 `flon.token` 发行的不可转让 GLON；GLON 由 `flon.system::addvote/subvote` 维护。
- 高风险或紧急操作可以继续使用 `flon.msig`，不重复实现多签。
- 论坛正文和评论保存在链下论坛；链上只保存不可变提案摘要、内容哈希和讨论地址。
- 治理参数先内置于 `dao.gov` 的配置表，不采用 mDAO 式的动态策略解释器，避免首版出现过大的执行和审计面。

## 2. 合约职责

### 2.1 `dao.gov`

`dao.gov` 是治理状态机和唯一的提案事实来源，负责：

- 治理参数：提案门槛、附议门槛、附议期、投票延迟、投票期、quorum、通过率、Timelock 和执行宽限期。
- 提案创建：记录提案人、类型、标题、摘要、正文哈希、元数据 URI、论坛讨论 URI 和待执行 action。
- 附议：GLON 持有人在附议期内支持提案进入正式投票。
- 票权快照：在提案进入投票阶段时冻结快照序号和总治理权，之后的 `addvote/subvote` 不改变该提案票权。
- 正式投票：支持 `for`、`against`、`abstain`；每个账户每个提案只能形成一份最终投票记录。
- 结果确认：按快照总票权计算 quorum，按有效赞成/反对票计算通过率。
- Timelock：通过后的提案必须等待安全延迟才能执行。
- 受控执行：仅执行创建提案时冻结的 action 列表，并检查合约/action 白名单、数量和数据大小限制。
- 生命周期：取消、失败、通过、排队、执行和过期。
- 可审计事件：重要状态变化均产生 action trace，索引服务据此生成治理时间线。

建议状态机：

```text
Draft
  -> Endorsement
  -> VotingDelay
  -> Voting
  -> Defeated
  -> Succeeded
  -> Timelocked
  -> Executable
  -> Executed
  -> Expired
```

建议核心 actions：

| Action | 功能 |
|---|---|
| `init` | 初始化治理合约和 GLON 数据源 |
| `setconfig` | 设置版本化治理参数；上线后只能由治理自身执行 |
| `setallow` | 管理可执行的目标合约/action 白名单 |
| `setpower` | 接收 `flon.system` 同步的 GLON 治理权检查点 |
| `propose` | 创建提案并冻结正文哈希、讨论地址和执行内容 |
| `setforum` | 在正式投票前补充或更正论坛讨论地址 |
| `endorse` | 附议提案 |
| `activate` | 达到附议条件后建立投票快照并进入投票延迟期 |
| `castvote` | 使用快照 GLON 投赞成、反对或弃权票 |
| `finalize` | 确认 quorum 和通过率，生成投票结果 |
| `queue` | 将通过的提案放入 Timelock |
| `execute` | Timelock 到期后执行冻结的 action 列表 |
| `cancel` | 在允许阶段取消提案 |
| `expire` | 清理超过执行宽限期的提案 |

### 2.2 `dao.treasury`

`dao.treasury` 只处理资产，不判断提案是否通过，负责：

- 接收 FLON 或治理批准的其他资产。
- 按已执行提案创建预算承诺，避免同一余额被重复分配。
- 一次性付款或按里程碑分期付款。
- 可选的收款方确认、执行期限和未使用预算释放。
- 提供余额、已承诺金额、已支付金额和提案关联记录。
- 仅接受 `dao.gov` 通过白名单 action 发出的指令。

建议核心 actions：

| Action | 功能 |
|---|---|
| `init` | 初始化允许资产和治理合约账户 |
| `setasset` | 管理可接收资产白名单 |
| `reserve` | 根据 proposal ID 预留预算 |
| `pay` | 执行一次性或里程碑付款 |
| `release` | 释放未使用预算 |
| `closegrant` | 关闭预算并保存最终状态 |

## 3. 现有合约需要的集成

### 3.1 `flon.system`

`flon.system` 仍是 FLON 投票本金和 GLON 发行/销毁的权威来源。需要在以下操作完成后同步治理权检查点：

- `addvote`
- `subvote`
- 历史账户迁移或修复操作

同步调用 `dao.gov::setpower(voter, power)`。`dao.gov` 在每次票权变化时生成全局递增序号，保存按账户排列的历史检查点，并在提案快照序号处查询票权。这样可以避免：

- 投票后立即撤回 FLON，但旧提案仍使用不正确的当前余额；
- 在投票期中增加本金，追溯性地影响已经开始的提案；
- 同一份 GLON 在历史快照计算中出现不确定结果。

### 3.2 `flon.token`

不需要新增治理 Token。继续使用现有不可转让 GLON，并保持：

- `addvote` 生成等量 GLON；
- `subvote` 立即销毁对应 GLON；
- GLON 不能在普通账户之间转账；
- FLON 节点分配和 GLON 社区治理用途相互区分。

### 3.3 `flon.msig`

普通参数提案通过 `dao.gov` Timelock 自动执行。以下操作仍建议走 `flon.msig` 或更长 Timelock：

- 合约代码升级；
- owner/active 权限修改；
- 非白名单资产的大额转移；
- 紧急暂停和恢复。

## 4. 论坛与提案讨论

论坛是链下产品，不单独部署链上论坛合约。建议数据关系如下：

```text
dao.gov proposal_id
  |- content_hash       提案规范化 JSON 的 SHA-256
  |- metadata_uri       IPFS/Arweave/HTTPS 的只读提案正文
  |- discussion_uri     论坛主题地址
  `- execution_hash     冻结的链上执行 action 哈希

Forum topic
  |- chain_id
  |- proposal_id
  |- proposal_tx_id
  |- content_hash
  `- comments / moderation / search / notifications
```

论坛集成规则：

1. 创建链下草稿和论坛主题，社区先讨论。
2. 创建链上提案时提交 `content_hash`、`metadata_uri` 和 `discussion_uri`。
3. DApp 同时校验链上 `content_hash` 与获取到的正文，避免论坛编辑导致提案内容被替换。
4. 评论、点赞、标签、搜索、举报和版主管理只存在论坛，不参与链上共识。
5. 论坛主题显示链上状态；链上提案页面嵌入或跳转到对应讨论。
6. 正式投票开始后，提案正文哈希和执行内容不可修改；可以继续发表评论。

论坛首选支持 Discourse 风格 API，也可以接入自建论坛。DApp/索引服务只依赖统一接口：

```text
POST /governance/topics
GET  /governance/topics/{chain_id}/{proposal_id}
GET  /governance/topics/{chain_id}/{proposal_id}/comments
```

## 5. 安全边界

- `dao.gov` 不接受任意目标 action；执行前必须通过双键白名单 `target_contract + action_name`。
- 提案创建时即冻结 action 数据，后续不能替换。
- 投票激活时冻结 quorum、通过率、Timelock 和执行宽限期，后续配置变化不追溯影响已经激活的提案。
- 执行 action 数量、单 action 数据和总数据大小均设置上限。
- Timelock 到期前不能执行，到期后超过宽限期自动失效。
- `setconfig` 和 `setallow` 上线后也必须通过治理提案执行，不能长期保留单一管理员后门。
- 论坛管理员不能修改链上提案、快照、票数或执行数据。
- 索引器和 Keeper 只负责查询和 permissionless 触发，不持有治理决定权。

## 6. 分阶段实施

### Phase 1：`dao.gov` 核心

- 配置、提案、论坛引用、附议、GLON 检查点、快照投票和结果确认。
- 完整单元测试：生命周期、重复投票、快照隔离、quorum、通过率和边界时间。

### Phase 2：Timelock 与受控执行

- 已实现双键 action 白名单、单 action 数据上限、执行哈希、排队、延迟执行、过期和原子失败回滚。
- `queue`、`execute` 和 `expire` 均为 permissionless action；调用者只推进已由链上规则确定的状态，不能更改执行内容。
- `execute` 在发送 inline action 前重新检查当前白名单和数据上限；安全委员会或治理撤销白名单后，尚未执行的提案会被阻止。
- `sealadmin` 可永久关闭启动期管理员入口；之后 `setconfig`、`setallow` 只能由 `dao.gov` 自身通过治理执行。
- 部署时必须配置最小权限 `dao.gov@eosio.code`，不授予宽泛 owner 权限。

### Phase 3：`dao.treasury`

- 已实现资产白名单与多 Token 合约/符号隔离账本。
- 已实现预算预留、分批付款、主动释放、permissionless 过期关闭和付款明细记录。
- 每笔 Grant 都关联 proposal ID、收款账户、总预算、付款次数上限、到期时间和预算说明哈希。
- `reserve`、`pay`、`release` 仅接受 `dao.gov` 授权；`closegrant` 只按已经冻结的到期时间释放未用预算，不转移资产，因此允许 permissionless 调用。
- 资金账本分别记录 `liquid`、`reserved`、`total_received`、`total_paid`，防止同一余额被重复预留。

### Phase 4：论坛和 DApp

- 论坛主题创建与绑定、内容哈希校验、讨论入口、通知和治理时间线。
- DAO Governance 页面接入真实提案、附议、投票、Timelock 和执行状态。

## 7. GLON 治理权同步

- `dao.gov::syncpower(voter)` 以 `flon.token::accounts` 中该账户的实际 GLON 余额为唯一事实来源。任何账户都可以触发同步，但不能传入或伪造票权数值。
- `flon.system::addvote` 铸造并转入 GLON 后，以及 `subvote` 收回并销毁 GLON 后，会内联调用 `syncpower`，立即写入新的治理权检查点。
- 合约上线前已经持有 GLON 的账户，可由本人、DApp 或 Keeper permissionless 调用一次 `syncpower` 完成初始化，无需管理员批量写票。
- `setpower` 只保留给受控迁移和恢复流程；生产环境的正常票权同步不得依赖人工传值。
- 提案激活时保存 `snapshot_sequence` 和 `snapshot_total_power`，之后增减 GLON 不会改写该提案的历史投票权。

## 8. 部署与初始化顺序

必须先在测试网完成全流程验证，再考虑主网。两个网络分别执行以下步骤，不能复用交易或 ABI 查询结果：

1. 由 `flonian` 创建 `dao.gov` 和 `dao.treasury`，owner 由 `flonian` 管理；上线阶段 active 也由 `flonian` 控制。
2. 部署经相同源码和 FLON CDT 版本构建的 WASM/ABI，并记录 SHA-256。
3. 为 `dao.gov@active` 增加且仅增加 `dao.gov@eosio.code`；为 `dao.treasury@active` 增加且仅增加 `dao.treasury@eosio.code`。不要授予 owner 或通配权限。
4. 执行 `dao.gov::init`：启动管理员使用 `flonian`，票权来源使用 `flon`，治理符号使用与链上 GLON 完全一致的精度。
5. 执行 `dao.treasury::init`，治理账户固定为 `dao.gov`。
6. 调用 `dao.gov::setconfig` 写入经过评审的门限、投票周期、Timelock 和 action 数据大小限制。
7. 仅为已审计的目标 action 调用 `setallow`；Treasury 首批通常只允许 `setasset`、`reserve`、`pay` 和 `release`，每项都设置独立的数据上限。
8. 升级 `flon.system`，使 `addvote/subvote` 自动同步 `dao.gov::syncpower`。升级前确认两个治理合约已部署并初始化，避免内联同步进入未初始化状态。
9. 对现有 GLON 持有人 permissionless 调用 `syncpower`，核对 `powers`、`checkpoints` 和 `global.total_power`。
10. 在测试网完成“提案 → 附议 → 快照 → 投票 → finalize → queue → Timelock → execute”以及 Treasury 预留/分期付款/释放/到期关闭全流程。
11. 验收后再通过治理执行 `sealadmin`，永久关闭启动管理员入口；不得在白名单和权限验证之前提前封闭恢复通道。

部署验收必须至少检查：链 ID、账户权限、链上 code hash、ABI、初始化表、GLON 同步、执行白名单、Timelock 边界、Treasury 账本守恒，以及失败交易不会留下部分状态。

## 9. 不采用 mDAO 多合约拆分的原因

`mdao.gov`、`mdao.propose`、`mdao.stg` 和 `mdao.stake` 可作为业务参考，但 FLON 已经具有 GLON、节点投票和系统权限体系。继续拆分策略解释器、提案、质押和治理配置会增加：

- 跨合约授权和故障点；
- ABI 与部署顺序；
- RAM 和 inline action 成本；
- 升级、迁移和审计复杂度。

因此首版采用两个职责清晰的新合约，并通过版本化表结构为后续扩展保留空间。

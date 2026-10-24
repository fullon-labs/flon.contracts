# 奖励 Epoch 与领取目标设计

## 设计结论

基础挖矿奖励和 GAS 节点奖励统一采用 **1 小时会计 Epoch**。Epoch 负责封存历史事实，领取频率与 Epoch 长度解耦：BP 可以每天领取一次，也可以由任意第三方代为触发，不会因为跨过 Epoch 而丢失奖励。第三方代触发只是普通 permissionless 交易，不要求运行 Keeper 服务。

### 基础挖矿奖励与 GAS 奖励的归属边界

两类奖励使用相同的出块统计事实，但不是同一个分配池：

```text
每个实际产生的区块
└─ 该区块的基础挖矿奖励毛额，100% 记入当前实际出块 BP 的 epochreward
   └─ Epoch 封存时应用该 BP 的绩效系数
      ├─ 不合格部分 forfeited，不转给其他 BP
      └─ 合格部分
         ├─ 按 reward_shared_ratio 划入 voter.reward，供该 BP 的历史有效 Voter 领取
         └─ 剩余部分计入该 BP 的 prodclaims / producers.unclaimed_rewards

每个 GAS 会计 Epoch
└─ 当期实际计量 GAS 对应 FLON × pool_bp（默认 10%）形成节点奖励池
   ├─ Active 池（默认节点池的 80%）：在多个实际参与出块的 Active BP 间，按 produced 比例分配
   └─ Standby 池（最多为节点池的 20%）：按合格 Standby 数量相对 30 个目标席位缩放，再等额分配
```

因此，“每块基础奖励给当前出块 BP”描述的是**毛额的初始归属**，不是承诺 BP 运营方最终保留全部金额；绩效扣减和 Voter 分享在 Epoch 封存时发生。GAS 奖励则从一开始就是独立的多节点 Epoch 池。Standby 不参与基础挖矿奖励，且 Standby 空缺对应的 GAS 上限不会由现有 Standby 瓜分。

目标 BP 入口为：

```cpp
flon::claimrewards(name caller, name producer, uint16_t max_steps)
```

- `caller`：签名并承担交易资源的账户，可以是 BP 本人或第三方；
- `producer`：唯一奖励受益人，基础奖励和 GAS 奖励只能支付给该账户；
- `max_steps`：本次最多执行多少个该 BP 的小时结算步，不能理解为处理多少个 BP；每一步最多各处理一个基础 Epoch 和一个 GAS Epoch；
- 推荐 DApp 默认值为 `24`，合约硬上限建议为 `48`；
- 基础奖励与 GAS 奖励共用工作量预算，并按 Epoch 结束时间从旧到新处理；
- 两类奖励账本、游标和审计字段保持独立，最终可以合并为一笔 FLON 转账。

Voter 使用独立入口：

```cpp
voter.reward::claimrewards(name caller, name voter, uint16_t max_steps)
```

第三方只能代触发。合约始终向 `producer` 或 `voter` 支付，`caller` 不能修改收款账户，也不默认收取佣金。
Voter 的 `max_steps` 上限为 30，限制本次实际结算的 BP 奖励位置数；若仍有未结算位置可以继续调用，全部处理后下一次空调用返回 `all rewards claimed`。

## 实施状态

代码已经实现版本化的一小时基础奖励时间轴、有界 `onblock` 封存、基础/GAS 共用出块 intrinsic 样本、历史票权封存，以及 BP/Voter 三参数领取 ABI。`closeepoch`、`beginsettle`、`settlenext`、`finalizeepoch` 已从公开 ABI 删除；相同的内部阶段由 `onblock` 自动推进。

这是一组需要按 `voter.reward → gas.reward → flon.system → DApp` 顺序协调发布的破坏性 ABI 升级。在三份合约均通过回放和测试网迁移验证前，不得部署到主网。

## 为什么按天领取不会有问题

领取不是按领取时刻重新估算过去一天，而是消费已经封存的小时记录：

```text
每小时封存一次
├─ Active BP 实际合格出块数
├─ 网络级异常标记
├─ GAS 池与节点服务绩效
└─ 当时有效的历史票权版本

BP 每天领取一次
└─ 顺序消费约 24 个属于自己的已结束 Epoch
```

因此每天领取通常在逻辑上需要最多 24 次轻量小时迭代。每次迭代最多推进该 BP 的一个基础奖励游标和一个 GAS 奖励游标，只读取并更新当前 BP 的记录，不能重新全表扫描或批量结算其他 BP。若 `max_steps` 小于积压小时数，保存两个奖励游标，下次继续；未领取余额持续累计且不过期。

## 周期与节奏

| 项目 | 周期或建议节奏 | 说明 |
| --- | --- | --- |
| 投票与排名生效 Epoch | 1 小时 | Epoch N 提交，N+1 起生效 |
| 基础挖矿奖励 Epoch | 1 小时 | 每块毛额归属该块实际出块 BP；Epoch 结束后逐 BP 应用绩效系数和 Voter 分享比例 |
| GAS 奖励会计 Epoch | 1 小时 | 每小时封存池与绩效，允许按天领取 |
| BP 资格考核窗口 | 滚动 6–24 小时 | 防止单小时网络抖动立即导致失格 |
| BP/Voter 领取 | 随时，建议每日一次 | 与会计 Epoch 解耦 |

## 封存与领取必须分离

Epoch 结束时只做确定性封存，不执行用户转账。封存必须保证后续任何领取顺序都得到相同结果：

1. 固定该 Epoch 的奖励池、出块绩效、GAS 绩效及网络异常状态；
2. 固定奖励产生期间的历史票权；
3. 推进下一 Epoch 的投票与排名生效边界；
4. 不向 BP/Voter 主动空投；封存 Voter 分享池时每块最多执行当前有界行对应的内部划拨，GAS 只写入 BP 的 `gasclaims`，最终奖励均由 `claimrewards` 拉取。

封存不依赖外部 Keeper。`onblock` 在检测到 Epoch 到期后进入有界状态机，后续每个区块最多处理一个基础奖励、绩效或票权行；GAS 内部结算每块最多处理 10 个节点行：

```text
OPEN → ENDING → APPLYING_DELTAS → SEALED
```

未完成时保存 phase 和 cursor，下一块继续。`N` 未完成封存时不开始 `N+1` 的封存，但新区块统计继续按其所属 Epoch 记录。只有 `SEALED` Epoch 能被领取接口消费。

### 每块统计开销

基础奖励与 GAS 必须共享同一次 `get_producer_slot_stats(from_slot, to_slot)` 结果，禁止在两个奖励模块重复调用 intrinsic。正常连续出块只处理当前 slot；少量缺口最多重建 480 slots，并按 Producer 聚合结果更新。超过 480 slots 时不在 `onblock` 中追赶，受影响 Epoch 标记为样本不完整并从当前 slot 恢复。

`detect_ended_epoch` 只执行 O(1) 的 slot 计算和 singleton 读取。`onblock` 不扫描全部 Voter 或历史 Epoch；需要划拨 Voter 分享池时，每块只执行当前有界行对应的内部转账，GAS 封存只写入有界的领取记录。

领取阶段只处理指定受益人：

```text
claimrewards(caller, producer, max_steps)
├─ 按结束时间选择该 producer 最旧的基础/GAS Epoch
├─ 使用封存数据计算应得金额
├─ 更新各自游标和幂等标记
├─ 累加基础奖励与 GAS 奖励
└─ 一次支付给 producer
```

Voter 奖励必须按奖励产生时的历史票权计算，不能使用领取时的当前票权。任何允许 BP 任意顺序结算、却没有历史票权快照的实现都不符合本设计。

## 工作量和超时控制

- `1 step = 为一个受益人推进一个小时窗口`，窗口内最多各结算一个基础奖励 Epoch 和一个 GAS 奖励 Epoch；因此两类奖励同时存在时仍是约 24 次循环，而不是 48 次；
- `max_steps = 24` 对应通常一天的积压，不代表固定执行 24 次；没有更多可结算记录时立即停止；
- 合约硬上限建议 `48`，避免长期未领取时单笔交易执行过久；
- 超过上限的积压通过后续交易继续处理；
- 单步必须为有界读取，禁止在每个 step 中扫描全部 BP 或全部 Voter；
- Epoch 封存、基础奖励游标、GAS 奖励游标和付款记录都必须幂等。

## 现网迁移约束

旧六小时 Epoch 的历史解释必须保留，但旧 action 不保留。原因是升级前的 `epochreward`、`bpstats`、`epochindex`、待领取余额和票权激活边界都使用旧 epoch ID；若升级后用 7,200 slots 从旧起点重新计算，同一 ID 会指向不同时间段，造成重复结算、遗漏或错误票权。`rewardcfgv2` 只在治理边界之后采用一小时公式，边界之前仍按 43,200 slots 读取。

升级时必须在治理确定的边界执行：

1. 先完成旧六小时 Epoch 和旧 GAS Epoch 的结算；
2. 记录旧状态机最后完成的 Epoch、slot 和未领取余额；
3. 从未来整点 slot 建立新的小时 Epoch 起点与新游标；
4. 验证旧余额保持可领取，且新旧区间无重叠、无空洞；
5. 新的统一领取 ABI 与 DApp 同一发布窗口切换。

基础奖励的版本化边界由系统权限设置：

```bash
fucli push action flon setrwdcfg '["<FUTURE_LEGACY_EPOCH_BOUNDARY>"]' -p flon@active
```

该时间必须在未来，并且必须与旧六小时 Epoch 边界对齐。边界前继续使用旧编号和 43,200 slots；边界后从相同的连续 Epoch ID 改用每小时 7,200 slots，避免重新解释历史表。

已有 GAS 配置需要先关闭旧结算、禁用奖励配置，完成所有旧 Epoch 后再以 `epoch_slots=7200` 重新启用。每个历史 GAS Epoch 自带 `gasepochcfg`，不会因新配置改变而重写。

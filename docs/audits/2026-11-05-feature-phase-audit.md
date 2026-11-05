# Valhalla Combat 全功能执行时态审查

日期：2026-11-05。源码基线：`364ff1740d0dfbce24bf8d8139c6635408f2e69f`。

本轮按用户要求，把玩法、一般逻辑错误、真正的时序与生命周期问题分开，覆盖所有已接入的功能入口。结合此前的 [运行时安全审查](2026-11-05-runtime-safety.md) 与 [处决诊断](2026-11-05-execution-diagnosis.md)。新增审查文档、受控探针并修正旧探针的判定，没有修改插件源码。

完整实施安排见 [全面修复与 SKSE Menu Framework 迁移计划](2026-11-05-remediation-plan.md)。

本文保留修复前证据；当前实现、回归与仍需实机核验的执行阶段见 [实施报告](2026-11-05-implementation.md)。

## 1. 判定标准与结论

| 类别 | 本报告的判定依据 | 例子 |
| --- | --- | --- |
| G：玩法或配置语义 | 不改变事件顺序，也能由数值、规则或资格条件解释；是否修改需要先选定预期行为 | 普攻眩晕倍率为 0，自动处决要求后续命中，按键不缓冲 |
| L：确定的逻辑缺陷 | 单线程、稳定对象、固定输入下仍可出错，不需要异步交错 | 候选 `return` 漏选、负格挡伤害、开关错误门控 |
| T：时序或生命周期缺陷 | 旧请求在状态改变后继续生效，或创建状态与回收状态分属不同阶段而失去对应关系 | 排队后死亡仍播放处决、重新按住后旧任务取消格挡、效果已创建但关闭设置后不回收 |
| S：内存、线程或 ABI 安全 | 未初始化值、引用所有权、共享数据竞争或调用约定；不能全部归入玩法或延迟 | 箭矢 cost 栈值、裸 Actor 指针、后台 RemoveSpell |
| V：现场待证 | 缺少实际引擎事件顺序、线程轨迹或目标版本反汇编，尚不能认定游戏内已发生 | 攻击停止与命中是否倒序、Hook ABI 差异、锁是否发生真实重入 |

类别可以重叠。例如后台延时删除法术同时涉及 T 与 S；自然恢复漏清 graph flag 是 L，不必用线程竞争解释。

**结论：其他功能确实存在同类的执行阶段缺陷，但不能把所有异常统称为时序。** 已复现的真实交错包括旧格挡任务干扰新按键、慢时间重叠互相截断、处决在队列执行时使用过时状态。另有独立的格挡公式、耗尽状态、功能开关和 UI 配置错误。

本轮受控探针执行当前函数体，在引擎替身上验证源码决策；没有运行 Skyrim 或完整 SKSE 插件。对象卸载后是否实际 UAF、引擎是否拒绝具体动画、物理回调是否并发，仍需现场证据。未证明栈溢出或 ABI 导致的栈破坏。

## 2. 全部已接入功能的覆盖表

表中位置均相对仓库根目录；A/B 编号对应原安全审查。

| 功能与入口 | 当前执行阶段及规则 | 审查结果 |
| --- | --- | --- |
| 轻击耐力与命中奖励：`attackHandler.cpp`、`animEventHandler.cpp` | preHitFrame 缓存，命中奖励并清缓存，attackStop 结算未命中；回调先于原动画 sink | G：逐命中奖励是否合理需玩法定义；V：乱序旧命中可能影响新攻击，尚无引擎轨迹；S：缓存查找未锁、锁后使用旧迭代器，条件性同步缺陷 A12 |
| 重击耐力及非战斗免耗：`Hooks.cpp:13` | 查询引擎重击消耗时决定是否返回 0 | G：非战斗免耗是配置规则；没有发现必须加延迟才能成立的证据。`staminaHeavyMiss/Hit` 未被当前主动路径调用 |
| 攻击/格挡期间耐力恢复：`Hooks.cpp:42`、`settings.cpp:41` | RestoreActorValue 调用点按当前攻击状态拒绝恢复、按倍率降低格挡恢复；设置保存种族原始值后应用倍率 | G：恢复限制与倍率；未发现反复更新造成倍率累乘。不能推断所有恢复来源都经过该调用点 |
| 物理格挡耐力/生命换算：`blockHandler.cpp:398` | 命中结算中同步改 HitData 和 AV | L01：不足时公式错误，可产生负伤害；L02：恰好耗尽遗漏 debuff；都不依赖异步 |
| 精准/完美格挡、惩罚与连击：`blockHandler.cpp:117`、`:435`、`:458` | 按键开窗，按游戏 dt 更新；完美格挡使用 hot 状态和累计次数 | G：失败窗口惩罚、累计判定是当前规则；T02/T07：按键状态与延迟释放脱节；L06：MCM 完美窗口滑条不消费 |
| 格挡承诺/延迟松开：`Hooks.cpp:213` | 分离线程按墙钟等待，再 AddTask | T02：旧任务无新按键身份，仍无条件 blockStop；S：布尔优先级可绕过 player/held 检查；不是已证实的栈对象悬空 |
| 冲撞 tackle：`blockHandler.cpp:531`、`:549`、`Hooks.cpp:180` | 原生碰撞与 Precision 入口分别判断 | L03：Precision 被近战精准格挡总开关提前拦截，冲撞单独开启不生效；V：同一击两条入口是否重复需现场追踪 |
| 箭矢/魔法弹反：`Hooks.cpp:110`、`blockHandler.cpp:199`、`:291` | collector 阶段判定/扣资源/播放效果，弹体改向与销毁进入任务队列；任务持有句柄引用 | S：普通箭矢 cost 未初始化 A01；L：首个角色返回 false 也终止扫描 B04；V：0xBE 签名差异 B01；L/V：改 owner 跳过物理 filter B05；延迟弹体任务缺少会话标识 T06 |
| 耗尽 debuff：`debuffHandler.cpp` | 耗尽时登记，update 满耐力移除，工作线程定期请求闪烁 | L04：自然恢复漏清 graph flag；T05：线程标志创建/停止竞态；T07：关闭 UI 影响已有状态清理 |
| 眩晕数值、破防与恢复：`stunHandler.cpp`、`stunHandler.h` | 命中扣值、健康恢复钩子补值，update 用游戏 dt 恢复 | G：普攻 0、属性公式、约 3+7 秒恢复；S：裸 actor 与容器/对象锁协议 A11/A13；不能仅因有多个回调就认定已发生数据竞争 |
| 倒地与恢复、受击反应：`reactionHandler.cpp`、`stunHandler.cpp:147` | 破防命中按当前开关发送倒地；恢复也检查当前开关 | T07：开启时倒地、关闭后恢复跳过，记录仍删除；L05：未知种族查询隐式插入分类；V：每次破防命中重发倒地是否打断动画需轨迹 |
| 手动/自动处决：`inputEventHandler.cpp:38`、`executionHandler.cpp` | 手动仅 keydown 尝试；自动查命中前破防状态；后台排队到游戏任务 | G：资格、绑定、后续命中；L：无效候选中止、最近无资格遮挡、空派发；T01/S：执行时不复查、裸指针、共享 RNG |
| 音效、火花、震屏、慢时间：`blockHandler.cpp:620`、`:647`、`:665`、`Utils.h:115` | 格挡回调播放即时效果，慢时间独立线程复位 | T03/S：慢时间重叠和后台改全局；S：生成火花返回值未检查 A07。即时效果调用不等于已证明物理锁重入 |
| Elden Counter：`blockHandler.cpp:712` | 同步 AddSpell，后台睡眠后直接 RemoveSpell | G：已有法术不刷新窗口可属于规则；T04/S：墙钟任务、裸对象、工作线程引擎修改 |
| TrueHUD：`ValhallaCombat.hpp`、`TrueHUDUtils`、两个 flash worker | 资源注册同步返回；special getter 由 TrueHUD 更新调用，Flash API 内部排 HUD 任务 | T05：worker 生命周期；S/V：眩晕共享数据的线程约束未建立；排除“Flash API 直接从后台改 GFx”的旧猜测 |
| 设置与数据：`settings.cpp`、`data.cpp`、`Utils.h` loader | DataLoaded 装钩后读配置/数据；设置回调直接刷新当前值 | L06：默认配置层不一致、未消费控件；T07：已有状态不随设置切换回收；S：异常和 loader 未初始化计数 A16/A17；没有证据称装钩顺序本身已造成启动竞态 |
| 读档、新游戏与暂停：`main.cpp:58`、`ValhallaCombat.hpp:61` | 暂停时跳过普通 update；PostLoadGame 只做部分清理，没有 PreLoad/NewGame 会话更新 | T06：旧排队任务/墙钟线程/攻击缓存/格挡状态没有统一失效；暂停时游戏 dt 与墙钟效果处于不同时间域 |

未计作活动功能：`balanceHandler.cpp`、`spiritHandler.cpp` 的注释遗留实现；未安装的 HasFlags1/魔法命中等 Hook；没有接入事件入口的 AI 辅助函数；空 `actorManager.cpp`、空 blockCommitment 更新；未注册的 cellLoad stub。它们不提供当前代码中所谓“另一个自动重试/清理阶段”。

## 3. 真正的时序和生命周期问题

### T01 / 高优先：处决排队时有效，执行时已失效

`src/bin/executionHandler.cpp:108`、`:112`、`:131`、`:148`。受控队列复现：排队后让目标死亡/3D 卸载、执行者 process 变空，任务仍进入一次配对播放调用。原检查不能覆盖之后的状态；50ms 睡眠在 AddTask 后，默认仅一次派发。应在任务执行时解析并保留句柄、复查必要条件，识别请求所属会话，并记录引擎返回值。

这与“普通攻击倍率小”不同：前者在等待中改变状态就改变正确性，后者即使完全同步也不会自动改善。

### T02 / 高优先：旧松开任务取消后来重新按住的格挡

`src/bin/Hooks.cpp:213`—`:229`。等待结束后虽然检查 held，最后仍无条件 `NotifyAnimationGraph("blockStop")`。真实函数体探针结果：普通松开时 ProcessButton=1、blockStop=1；排队后重新按住时 ProcessButton=0，blockStop 仍为 1。

另外 `src/bin/blockHandler.cpp:125` 在旧窗口存在时先返回，`:139` 的 held=true 尚未执行。down→up→down 后 held=false 已复现。这里不要求改变精准格挡的窗口规则：是否重新开窗与物理键是否按住，应是两个独立状态。

`:220` 的 `... && (...) || IsStaggered()` 还能绕过前面的非空和 held 限制。没有证据证明 `PlayerControlsData*` 必定来自已返回的栈帧；此前已核对其上游为 PlayerControls 内嵌数据，不能直接称 use-after-stack。

### T03 / 高优先：慢时间任务互相覆盖，未恢复先前速度

`src/include/Utils.h:115`—`:124`。每次调用立即 SGTM，再独立等待并写回 1。受控运行重叠 50ms、300ms 两个请求，第一个结束时倍率已经变成 1，第二个尚未到期；原先倍率 0.4 最后也被覆盖为 1。

这是已复现的请求所有权错误。游戏暂停只阻止普通 update，不能阻止该工作线程的墙钟计时和全局写入。应统一在引擎允许的更新阶段管理慢时间，明确重叠合并与前值恢复规则。

### T04 / 高优先：Elden Counter 延时删除脱离游戏执行阶段

`src/bin/blockHandler.cpp:722`—`:728`。真实 worker 探针确认 RemoveSpell 的调用线程就是工作线程，不是调度主线程。裸 Actor* 经过 sleep 后仅检查地址是否为 null，无法证明对象仍有效；暂停、读档也没有取消机制。应把定时与实际删除分开，在适当的游戏阶段重新解析目标并检查请求是否仍属于当前效果。

已证明调用阶段与所有权缺失，没有通过此探针声称实际游戏崩溃。已有法术时是否刷新持续时间则是另一项玩法选择。

### T05 / 中优先：耗尽提示 worker 的开始、停止不形成完整状态机

`src/bin/debuffHandler.cpp:72`—`:89`。先创建线程、再把 active 写 true，允许 worker 先读 false 并退出；stop 后马上 restart，旧 worker 可能在醒来后看到新 true 而继续，形成重复 worker。单个 atomic bool 不区分不同轮次的效果。

关闭 bUIAlert 后 revert 提前返回，不会清 active；受控探针得到 flag=1，而开关保持开启的对照能清为 0。TrueHUD Flash 自身会排 HUD 任务，问题应定位到本 mod 的 worker 生命周期。stun flash 的 check-then-set 和异常退出不复位也需要同类修正，见 A14/A11。

### T06 / 高优先：旧会话工作未失效

`src/bin/main.cpp:58`—`:78`。PostLoadGame 只停止玩家 debuff、清 stun、更新游戏设置；没有统一回收旧 execute/unblock/slowTime/EC 任务、攻击缓存和格挡计时，也没有 PreLoad/NewGame 世代。

因此旧请求仍可在新存档场景执行；句柄有效也只能证明当前对象可解析，不能证明旧意图仍适用。用“会话已失效”取消旧工作，并在明确的阶段回收本插件创建的状态，比增加随机等待更符合问题本身。

### T07 / 中优先：热切换设置后，旧状态缺少回收路径

- **倒地**：`src/bin/stunHandler.cpp:147` 用当前 bDownedStateToggle 决定是否恢复，而不是记录是否曾经让该角色倒地。先倒地→关开关→恢复时，真实 update 仍删记录但不调用恢复；探针对照为 toggle on：downed=0，toggle off：downed=1，二者记录均清空。
- **恢复前卸载**：`src/bin/reactionHandler.cpp:126` 的恢复可能因 3D/游泳/killmove 条件提前返回，调用者随后删除记录，没有成功反馈或重试。具体 graph 后果需游戏验证。
- **格挡键**：两个精准开关关闭后，`src/bin/blockHandler.cpp:170` 的 keyup 直接返回，旧 held 无法清除；探针确认。alt-block 的输入入口本身也由当前开关门控。
- **耗尽 UI**：关闭 UI 后才清理旧效果，会落入 T05 的停止遗漏。

共同原则：开关决定是否创建新状态；对本插件已经创建的状态仍要完成对应清理。不能只把所有读取设置的地方改成更晚执行。

## 4. 确定的普通逻辑错误

### L01 / 高优先：格挡不足时伤害公式可写入负值

`src/bin/blockHandler.cpp:418`—`:420`。设原始伤害 100、难度换算为 1、耐力消耗倍率 m，当前先得到 staminaDamage=100m，再计算 `(staminaDamage - stamina/m) × 100/staminaDamage`，混用了耐力与生命伤害单位。

| m | 当前耐力 | 实际写回 HitData.totalDamage | 按耐力承受比例的线性换算 |
| --- | --- | --- | --- |
| 1 | 99 | 1 | 1，正常对照 |
| 0.5 | 40 | **-60** | 20 |
| 2 | 50 | 87.5 | 75 |

真实函数体探针复现以上数值；这些倍率可在发布包 MCM 中配置。这是算式错误，不是时序。负值最终如何被游戏消费仍取决于后续引擎处理，不宣称已经证明会治疗或崩溃。

### L02 / 中优先：刚好扣空与扣不够走了不同耗尽处理

`src/bin/blockHandler.cpp:409` 的条件是 `<`。耐力 100、消耗 100 时走 else，扣到 0 但没有 checkStamina/initDebuff；耐力 99 则立刻初始化耗尽。探针对应 debuff 调用数 0 与 1。之后某个动画事件也许会补检查，但当前结算缺少状态更新是确定的，不需要假定多线程。

普通 timed block 在 `:517` 扣耐力后同样没有统一的耗尽检查；其是否被后续事件补上要看具体动画。应统一处理耗尽边界，不应依赖不相关的 attackStop 恰好发生。

### L03 / 中优先：Precision 冲撞被无关开关拦截

`src/bin/blockHandler.cpp:549`—`:579`。开头 `!bTimedBlockToggle` 就返回，后面独立 bTackleToggle 无机会运行。探针：近战精准格挡 off、冲撞 on 时 tackle 调用 0；只把前者改 on 就调用 1。原生 melee collision 路径没有该总门控。这是集成入口的逻辑不一致，不是等待时间太短。

### L04 / 中优先：耗尽自然恢复漏清动画变量

`src/bin/debuffHandler.cpp:13`—`:42`。满耐力自然恢复删除集合/玩家 perk/UI，却不设置 gv_bExhausted=false。探针得到 tracked=0、graph_exhausted=1；再调用 stopDebuff 因不在集合内而返回，变量仍为 1。显式停止且仍在集合的对照能清为 0。

这属于清理遗漏。是否导致特定行为动画继续异常，取决于行为图使用该变量的方式。

### L05 / 中优先：查询受击反应时改变处决种族资格

`src/bin/reactionHandler.cpp:84` 使用 `raceMapping[race]`，未知种族会被插入默认 Humanoid。于是先前被处决拒绝的自定义种族，在某次受击反应后可能被接受。这里有先后顺序影响，但根因是只读查询的写入副作用；无需线程竞态。更不能据此证明骨架和 idle 已兼容。

### L06 / 中优先：多个设置“界面可改，当前逻辑未消费”

- 发布包 fPerfectBlockWindow 控件无 DLL 消费点；实际使用 uPerfectBlockAccum 累计次数。不能把这解释成设置更新延迟。
- 只读用户 override，不读取包内 defaults，导致新安装 bBlockedHitRegenStamina 等界面默认与 DLL 默认不一致。MCM 不会自动把全套默认写入 override，见 B02。
- fStunCounterAttackMult 被加载，但 guard counter 的两个分支均使用同一 powerAttack 路径，没有消费该倍率。
- bExecutionLimit 被加载却没有实际等级限制分支；bPlayerExecution 也没有主动消费者。不能用这些开关推断“开荒限制”或玩家处决支持。

处决的候选中止、最近不合格者遮挡、空武器分支假成功也归 L，详见处决诊断第 3、4 节。空 idle 容器则另有 S 类未定义行为，不只是返回结果不正确。

## 5. 不应当直接认作缺陷的玩法与待证项

| 现象或怀疑 | 当前判断 |
| --- | --- |
| 普攻杀死敌人时眩晕还很多 | G：默认普通攻击倍率 0、max=(永久生命+永久耐力)/2；先核对数值与玩法预期 |
| 清空不立刻自动处决 | G：当前检查的是命中前已破防；后续普通近战命中可触发，盾击跳过。是否改为同击需要定义伤害接管规则 |
| 提前按住处决键，空条后没有执行 | G：只响应 keydown，不缓冲未来机会；是否需要缓冲是输入设计 |
| 破防后还能恢复、过一会失去处决资格 | G：最后一次有效伤害后约 3 秒延迟加约 7 秒恢复；非只开放零值一帧 |
| mod 敌人 essential、未映射种族或无兼容动作 | G/兼容性：与眩晕伤害资格独立；不能统称为队列延迟 |
| 一次攻击命中多个目标，获得多次奖励 | G/V：当前按命中结算；未定义每次挥击上限，不能凭现象认定重复事件错误 |
| attackStop 比命中早、旧命中落在下一击缓存上 | V：源码缓存没有挥击身份，机制上脆弱；需要真实事件序列才能证明现场发生 |
| 两个碰撞入口、多个 collector 回调重复扣资源 | V：缺少现场调用关联；不能仅因存在两种入口就宣布每击结算两次 |
| 眩晕容器访问已造成 race 或死锁 | S/V：锁覆盖不完整、shared_ptr 不保护 pointee、跨外部调用持锁均可确认；实际并发/锁重入仍待线程记录 |
| Projectile 0xBE 签名已损坏栈 | V：与声明依赖的 bool HandleHits(hkpCollidable*) 有差异，但未获目标运行时反汇编，不宣称已证实 ABI 崩溃 |

原处决 flow 探针已改为观察性通过：确认当前规则，不再以尚未选定的“破防当击自动处决”期望标成失败。

## 6. 探针与处理顺序

工具：[feature_probe.py](probes/feature_probe.py)，复用 [execution_probe.py](probes/execution_probe.py) 的真实函数提取器。Windows + Python + clang++ + VS C++ Build Tools；临时生成 C++/exe 后清理，不加载游戏 DLL。

```powershell
python docs/audits/probes/feature_probe.py --case block
python docs/audits/probes/feature_probe.py --case debuff
python docs/audits/probes/feature_probe.py --case timing
python docs/audits/probes/feature_probe.py --case stun
python docs/audits/probes/feature_probe.py --case worker
python docs/audits/probes/execution_probe.py --case flow
```

| case | 直接执行的真实函数 | 本次输出 |
| --- | --- | --- |
| block | onBlockKeyDown/Up、processStaminaBlock、precisionPrehitCallbackFunc | [按键、公式、耗尽与冲撞](probes/feature-block-result.txt) |
| debuff | update、stopDebuff、revertDebuffUI | [自然恢复与 UI 清理](probes/feature-debuff-result.txt) |
| timing | unblock_delayed_threadfunc、slowTime | [旧松开任务与慢时间重叠](probes/feature-timing-result.txt) |
| stun | stunHandler::update | [倒地开关切换与恢复](probes/feature-stun-result.txt) |
| worker | async_removeECTriggerSpell | [RemoveSpell 调用线程](probes/feature-worker-result.txt) |
| flow | hitProcessor::processHit | [自动处决的当前规则](probes/flow-result.txt) |

这六组本次退出 0，含义是观察与当前代码一致，缺陷也按预期复现，**不是修复通过**。旧 selection/dispatch/timing 仍保留诊断失败标准。新 block 探针把难度换算固定为 1 并独立控制倍率；debuff 用受控 AV/graph 替身；stun 把恢复条件设为已满足、只验证是否调用恢复；worker 让 Actor 持续存活并 join，只确认线程归属，不试图制造 UAF；timing 控制任务 drain 顺序并等待两个真实慢时间 worker 结束。

建议实施顺序：

1. 先修不依赖玩法决策的 S/L：箭矢未初始化值、空 idle、格挡公式、候选漏选/空派发、耗尽清理、Precision 门控；ABI 单独核实。
2. 统一 T 类请求的执行阶段和失效条件：处决、延迟松开、EC、慢时间、读档取消；为旧任务与新意图建立身份。
3. 修补已经创建的效果在设置关闭、目标卸载、自然恢复时的成对清理；建立明确的线程约束后收敛容器与 pointee 锁。
4. 最后决定 G 类玩法：普攻削韧、同击自动处决、输入缓冲、每次挥击奖励上限。没有设计选择前，不将它们混入时序修复。

需要游戏内追加的最小轨迹是：事件/请求编号、帧号、线程号、存档会话号、ActorHandle、当前状态、拒绝原因、引擎返回值。用它确认 attackStop/命中顺序、碰撞重复与动画接受情况；不需要为已确定的算式和清理缺陷再等待现场日志。

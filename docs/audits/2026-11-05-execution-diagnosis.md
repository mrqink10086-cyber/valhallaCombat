# Valhalla Combat 眩晕与处决反馈诊断

日期：2026-11-05。基线：`364ff1740d0dfbce24bf8d8139c6635408f2e69f`。

按用户约定，用当前实现分析原版 1.3.3 的反馈；[0.2.0-NG 发布说明](https://github.com/blink-blink/valhallaCombat/releases/tag/v0.2.0-NG)未声称修复处决。前两轮完整审查见 [运行时安全与发布审查](2026-11-05-runtime-safety.md)。本次新增文档和独立诊断探针，没有修改插件实现。

完整实施安排见 [全面修复与 SKSE Menu Framework 迁移计划](2026-11-05-remediation-plan.md)。

本文和诊断探针固定于修复前基线；当前候选筛选、派发和结果回报的修复状态见 [实施报告](2026-11-05-implementation.md)。

## 结论与证据范围

**三类现象不能统一归因于时序。** 普攻默认不造成眩晕伤害、属性公式、后续命中才触发自动处决，属于当前玩法或配置语义；候选提前退出、空派发属于逻辑缺陷；排队时检查与执行时状态脱节才是已复现的真实时序缺陷。全功能分类见 [全功能执行时态审查](2026-11-05-feature-phase-audit.md)。

| 用户反馈 | 当前实现中的直接解释 | 证据强度 |
| --- | --- | --- |
| 敌人先死，眩晕还没空 | 普攻倍率默认 0；重击阈值按永久生命、耐力计算，未保证在生命耗尽前达到；命中健康恢复钩子还会补眩晕 | 公式和受控数值运行已确认；实际战斗贡献取决于配置与命中数据 |
| 耐打的 mod 敌人空条后仍无法处决 | 种族未映射、essential/麻痹等资格限制；若干种族无徒手分支；候选选择无后备；动作还可能被引擎拒绝 | 拒绝与空派发路径已复现；具体 mod 的种族、标记和动作兼容性需现场记录 |
| 开荒容易空条，按键没反应 | 一个无效候选会终止整个搜索；最近但不合格的候选遮挡其他目标；排队后状态变化没有复查 | 真实函数体的受控运行已复现；不能据此判定每次游戏内无响应都由同一原因造成 |

探针直接提取当前 C++ 函数体，使用 clang++ 20.1.0 编译，在受控的 Actor、任务队列和引擎接口替身上执行。复现的是选择、数值、调用顺序和派发决策；没有模拟游戏动画、物理或 ABI，也未运行完整 SKSE 插件。

## 1. 为什么会先打死敌人

位置：`src/include/settings.h:157`、`src/include/stunHandler.h:27`、`src/bin/stunHandler.cpp:277`、`src/bin/stunHandler.cpp:294`。

当前默认值与随包默认设置一致：

```text
普通攻击倍率 = 0
重击倍率 = 1
匕首倍率 = 0.5，单手剑倍率 = 1，双手武器倍率 = 1.2
最大眩晕 = (永久生命 + 永久耐力) / 2
普通/重击眩晕伤害 = 传入伤害 × 攻击倍率 × 武器倍率
```

因此，连续普攻仍可降低生命，而眩晕扣除为 0。探针对同一目标传入十次 100 点普攻伤害后，眩晕保持 `100/100`，实际 `damageStun` 调用为 0；仅将普通攻击倍率改为 1，传入一次 20 后变成 `80/100`。

即使全用重击，也没有“眩晕必定先于生命耗尽”的保证。例如永久生命 100、永久耐力 300 时，最大眩晕为 200；单手剑五次传入 20 点伤害，只能将眩晕降至 `100/200`。如果这五次也恰好消耗目标现有的 100 点生命，目标先死是公式允许的结果。这个例子假定眩晕计算的传入伤害与生命损失相等；游戏中的护甲、难度、额外伤害和恢复会改变比例。

另一个耦合位于 `src/bin/Hooks.cpp:55`：到达该健康恢复钩子的恢复量会等量传给 `modStun`。探针验证 `modStun(+50)` 将 `50/200` 补成 `100/200`。这不证明所有药水、脚本回血或 mod 治疗都会经过这个特定调用点。

判断：先检查生效的普攻倍率、敌人永久耐力和健康恢复路径，比直接归因于执行延迟更有依据。仅增加处决重试不能改变这些数值关系。

## 2. 清空眩晕不会立即执行动作

破防时，`src/bin/stunHandler.cpp:226` 将目标加入 `stunBrokenActors`、启动提示并允许战斗中恢复。它没有直接调用处决。

### 2.1 自动处决检查比本次扣眩晕早

位置：`src/bin/hitProcessor.cpp:53`、`src/bin/hitProcessor.cpp:65`。

当前普通近战命中的实际顺序是：

```text
读取命中前是否已破防
    → 若已破防，按 bAutoExecution 等条件尝试处决
    → 扣除本次攻击的眩晕值
    → 外层 Hook 调用游戏原始命中处理
```

对“本次攻击恰好清空”的目标，前面的破防判断仍为 false。开启自动处决的探针结果：

```text
auto_breaking_hit: broken=1 attempts=0
auto_following_hit: attempts=1
```

盾击在 `src/bin/hitProcessor.cpp:33` 的路径扣眩晕后直接返回，因此破防盾击及后续盾击均未进入自动处决检查。弹反扣眩晕也不是这个普通命中检查入口。

这是已确认的调用语义：**自动处决由“已破防目标被后续近战命中”触发。** 是否要改为破防当击触发属于行为设计；当前顺序确实能解释“空条后没有自动发生动作”。若破防一击本身致死，后续触发机会也会消失。

外层 `src/bin/Hooks.cpp:91` 在 mod 处理后仍调用原始命中函数。单独移动处决判断或增加 sleep，不能保证配对动作在致死伤害之前成立；若要支持破防当击处决，还需要明确该次伤害与动作接管的关系。

### 2.2 手动键与自动开关是两条入口

位置：`src/bin/events/inputEventHandler.cpp:38`、`src/include/settings.h:173`。

手动键仅在 `IsDown()` 时尝试一次，不会持续等待以后出现的破防目标。提前按住处决键，再打空目标，不会因此重新触发。默认 `iExecutionKey=-1`、`bAutoExecution=false`，必须分别设置；自动开关并不控制手动入口。

未假定用户没有绑定键。这些是排查前提，不是本次认定的用户配置错误。

## 3. 手动搜索存在两个可复现的漏选问题

### 3.1 一个无效候选终止整个搜索

位置：`src/bin/executionHandler.cpp:17`—`src/bin/executionHandler.cpp:25`。

循环遇到有 handle、但解析失败、死亡、3D 未加载、正在 killmove 或不在 high process 的目标时，执行 `return`。这会退出整个函数，而不是跳过该目标。

最小受控场景：只有一个有效近距离目标时成功；在枚举中加入先被访问的死亡目标或未加载目标后，同一个有效目标不再得到尝试。

```text
control_valid_only: PASS
valid_target_after_dead_candidate: FAIL (attempts=0, expected=1)
valid_target_after_unloaded_candidate: FAIL (attempts=0, expected=1)
```

真实集合是 `unordered_set`，探针固定的是一种可发生的枚举顺序，并非宣称游戏总按加入顺序遍历。即使已经找到有效目标，后续遇到一个上述无效对象也会在最终调用前退出。死亡目标通常在下一次 stun update 被清理，所以死亡遮挡可能短暂；仅因 3D 未加载或不在 high process，并不会在该 update 中立即被清理。

修复方向：无效对象只跳过，不终止整个按键请求。

### 3.2 先选最近目标，再验证资格，失败不换人

位置：`src/bin/executionHandler.cpp:28`、`src/bin/executionHandler.cpp:36`、`src/bin/executionHandler.cpp:41`。

搜索先从破防集合中选择距离小于 250 的最近目标，随后才进入 `attemptExecute` 验证种族、essential、武器等。若最近者不合格，不会尝试稍远的合格者。

实际函数体的探针分别用距离 50 的未映射种族和 essential 目标，遮挡距离 100 的可处决目标，两次最终派发数均为 0。相同可处决目标单独存在时，派发数为 1。

修复方向：先筛选可处决目标再排序，或按距离尝试并在失败后继续。资格判断应与实际动作可用性共用，避免再次出现“通过检查但无动作”。

## 4. mod 敌人仍可能过不了种族、武器或动作条件

位置：`src/bin/executionHandler.cpp:43`、`src/bin/executionHandler.cpp:59`、`src/bin/data.cpp:207`。

破防伤害不要求目标存在于处决种族映射中；处决却查找具体 `TESRace*`。因此，“能空条”不能证明“有匹配处决”。essential、麻痹、骑乘、队友等条件也独立于眩晕条。

这里的映射还有状态副作用：`src/bin/reactionHandler.cpp:84` 使用 `raceMapping[race]` 查询，遇到未知种族会插入默认值；`src/include/data.h:122` 的默认分类是 Humanoid。于是自定义种族可能最初被拒绝，之后因受击反应被当作 Humanoid。这个插入不证明其骨架和配对动作兼容，也不能把所有 mod 敌人概括为“始终未映射”。

若干非人种族的徒手分支只有 `break`，例如 `src/bin/executionHandler.cpp:188` 的尸鬼。上层 `attemptExecute` 仍在末尾返回 true。探针保留真实尸鬼分支得到：

```text
mapped_sword_control: accepted=1 queued=1
unmapped_mod_race: accepted=0 queued=0
mapped_undead_unarmed: accepted=1 queued=0
```

最后一行是可确认的“成功返回但没有请求动作”。真实动画表缺失时还有独立的空列表未定义行为，见安全审查 A03；本次没有运行空容器解引用来制造崩溃。

修复方向：种族查询避免隐式插入；让每个武器/种族分支返回实际派发结果；缺少动作时明确拒绝，并记录具体原因。现场针对 mod 敌人应记录 Race FormID/插件、essential、武器类型和实际 idle。

## 5. 排队检查与执行时状态脱节

位置：`src/bin/executionHandler.cpp:108`、`src/bin/executionHandler.cpp:131`、`src/bin/executionHandler.cpp:148`；`src/include/offsets.h:53`。

流程为：

```text
按键/命中时通过检查
    → 分离线程持有裸 Actor*
    → AddTask 排入游戏任务队列
    → 任务直接访问 currentProcess 并调用 playPairedIdle
```

任务没有重新验证双方存活、3D、当前 process、破防资格或会话。配对播放接口返回 bool，但包装函数忽略它，因此“已尝试”也无法区分“引擎接受了动作”。

受控队列探针运行真实单 idle worker 和播放包装函数：正常状态执行时引擎调用成功；任务排入后改变对象状态，再 drain 队列，仍发生一次对过时状态的引擎调用。替身接口拒绝该请求，原代码没有反馈或重试。

```text
queued_valid_control: calls=1 stale=0
state_changed_before_task_drain: calls=1 stale=1
```

此探针让 Actor 对象继续存活，所以它验证的是缺少执行时复查，并未声称复现了真实 UAF 或游戏崩溃。

两种 worker 中的 50ms sleep 都发生在 `AddTask` 之后；默认 `i=1`，这既不是保证等待动画就绪，也不是有效重试。任意增加等待会扩大状态变化窗口。

修复方向：在统一执行阶段解析 ActorHandle 并保留引用，重新检查状态与会话；同一对角色维护待执行请求；收集配对播放的实际返回结果。原安全审查 A02、A04 还记录了裸指针生命周期和共享随机数生成器问题。

## 6. 破防资格有恢复期限；未发现等级分支

位置：`src/include/stunHandler.h:33`、`src/include/stunHandler.h:73`、`src/bin/stunHandler.cpp:135`、`src/bin/stunHandler.cpp:230`。

破防后开启战斗中恢复。默认最后一次眩晕伤害之后先等约 3 秒，再按每秒最大值的 1/7 恢复；没有新的有效眩晕伤害、额外回血等影响时，约 10 秒恢复满并退出破防集合。恢复阶段仍在集合中，**并非只有数值恰好为零的一帧可处决**。探针确认战斗中标志开启后，连续 7 秒恢复可从 0 回到最大值。

`bExecutionLimit` 虽然被加载，但搜索整个 `src` 只有声明与加载，实际处决路径没有消费它，也没有玩家等级判断。因此“开荒阶段”的差异更可能对应伤害、属性、武器、目标和配置变化，现有代码不支持直接归因于等级限制。

## 7. 可重跑的诊断记录

工具：[execution_probe.py](probes/execution_probe.py)。需要 Windows、Python 3、clang++ 和 Visual Studio C++ Build Tools。临时生成的 C++ 与 exe 在临时目录中编译执行后清理；插件源文件不改写。

在仓库根目录执行：

```powershell
python docs/audits/probes/execution_probe.py --case selection
python docs/audits/probes/execution_probe.py --case numeric
python docs/audits/probes/execution_probe.py --case dispatch
python docs/audits/probes/execution_probe.py --case flow
python docs/audits/probes/execution_probe.py --case timing
```

| case | 保留的真实代码 | 本次结果与解释 | 原始输出 |
| --- | --- | --- | --- |
| selection | tryPcExecution | 退出 1：单有效目标对照通过；混入死亡/未加载候选后漏选 | [selection-result.txt](probes/selection-result.txt) |
| numeric | actorStunData、processStunDamage、C++ 默认值 | 退出 0：数值预测通过；包含改变一个普攻倍率的对照 | [numeric-result.txt](probes/numeric-result.txt) |
| dispatch | attemptExecute、executeDraugr、tryPcExecution | 退出 1：徒手空派发、最近不合格目标遮挡 | [dispatch-result.txt](probes/dispatch-result.txt) |
| flow | hitProcessor::processHit | 退出 0：确认当前规则为后续近战命中触发、盾击跳过；不把玩法选择计为故障 | [flow-result.txt](probes/flow-result.txt) |
| timing | 单 idle worker、playExecutionIdle | 退出 1：排队后状态变化仍进入引擎调用 | [timing-result.txt](probes/timing-result.txt) |

selection 是按漏选症状建立的失败循环，重复执行结果一致；其余探针分别隔离数值、资格、调用顺序和异步边界。flow 只检查当前调用语义；此前采用“破防当击自动处决”的期望作为失败标准不够准确，已改成观察性通过，避免将尚未选定的玩法设计写成时序错误。

selection、dispatch、timing 保留缺陷失败结果用于诊断；numeric、flow 的退出 0 表示数值或规则观察符合当前实现，没有做源码修复。引擎为何拒绝具体 idle、用户现场是否经过健康恢复钩子，以及输入事件是否到达，仍需游戏内轨迹才能定案。

## 8. 建议的处理顺序

1. 修正候选循环与资格筛选，消除能确定复现的手动键漏选。
2. 让种族/武器分派和引擎播放返回真实结果，缺少 idle 时停止；种族只读查询不再插入分类。
3. 将处决请求统一到有句柄、执行时复查和去重的游戏任务中，消除异步状态与裸指针问题。
4. 明确自动处决采用破防当击还是后续命中，再调整调用顺序和致死伤害处理。
5. 最后按预期战斗节奏调整眩晕倍率、属性公式与回血耦合；调数值不能替代前面的漏选和派发修复。

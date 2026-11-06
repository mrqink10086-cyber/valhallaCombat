# 2026-11-06 实际源码审核、修复与测量

基线：`Master` / `1213bae`，工作树中的实际 C++ 逻辑。三个 `gpt-6-luna` / `xhigh` 子代理分别审核内存与线程、需求与时序、对抗验证；主代理重新检查代码、回归及关键引擎反汇编。以下结论来自本轮检查，不以旧审核报告为依据。修改尚未提交。

## 已修复的实际路径

| 路径 | 原有问题 | 修复及验证 |
| --- | --- | --- |
| 玩家/NPC 攻击开关 | 上游调用点用玩家开关挡住 NPC 独立配置；受击方疲劳逻辑也被攻击方配置耦合 | `attackHandler::enabledFor` 按对应 actor 选开关；命中奖励、动画登记与受击方逻辑分别判断；四种开关组合回归 |
| 动画与命中顺序 | worker 的 preHitFrame 尚在队列中，owner 命中先发奖励，随后 attackStop 又结算落空 | 命中处理先消费已到达的动画事件；worker/重入事件带 handle、session 排队，合并唤醒；owner 常见事件直接处理；reset 清队列；回归覆盖重入和旧 session |
| 调度器 | 每帧扫描所有 pending，密集到期/取消反复线性查找和移动元素 | ticket 索引加 deadline 有序集合；保持提交顺序、同批取消及嵌套 next-update 语义；补 catch-all 防止非标准异常搁置后续任务 |
| 定时格挡与 tackle | 新窗口结束时把整帧 delta 再扣一次 cooldown；大帧不能追过多档惩罚；空闲后仍持续更新 | 只传递 overshoot；循环追赶惩罚档位；全部定时状态结束时停用 update；reset 同步停用；覆盖举盾冻结、松盾恢复、零周期和大 delta |
| stun 恢复 | 3 秒等待结束所在帧的剩余时间被丢弃 | 等待只消费所需 delta，余量进入 7 秒恢复；3.5 秒及后续 6.5 秒回归 |
| stun 快照 | 每次命中、修改及每帧恢复重建整张 map，反复分配 | 单项发布与批量 delta；复用连续 vector 容量，查询仍受 mutex 保护；并发读者、重入 reset、卸载后重试清理回归 |
| projectile 拦截 | 一次成功碰撞分拆多个延后任务 | 合并成一个 handle-based 任务，generation 校验及 RAII 清除 pending；Havok 对象变更仍离开碰撞栈后执行；覆盖过期对象、失效 session/代次、异常与拒绝调度 |
| projectile 旋转 | 垂直方向除零产生 NaN，asin 输入可能越界 | 有限垂直基底、水平归一化及 clamp；覆盖上下垂直、近垂直、一般方向和正交性 |
| 数值及可选资源 | 余额恰好等于成本、零余额零成本被拒绝；可缺失音效 descriptor 被解引用 | 允许精确支付与零成本，拒绝负值/NaN；音效入口空指针保护；保留引擎声音句柄类型 |
| hook 入口 | 成本 hook 在 ready/null 检查前解析 owner；SE GetWantBlock 缺少会话检查 | 提前检查；未就绪时保留原函数结果 |
| 生命周期线程与顺序 | 实机 DataLoaded 在 thread 55352，插件 Load/PostLoad 在 1632；DataLoaded 直接绑定 runtime owner，主菜单自动自测未输出；owner 直接执行又可能越过先前 worker 排队的 reset | 初始化、加载/卸载/新游戏及菜单生命周期经 `dispatchLifecycle` 统一进入 SKSE FIFO，避免混合 inline/queued 乱序及旧 epoch 过滤吞掉 session 转换；新增 mixed-thread 反例在旧 helper 失败，修复后通过。重启后初始化及主菜单自测均在线程 55632 执行；战斗 hook 线程待对应场景采集 |
| trace 文件写入 | 菜单事件 sink 内同步 flush，磁盘操作占用引擎事件源 spinlock | flush 排到 SKSE task，在事件回调返回后执行 |

实际活路径以 actor/projectile handle 跨帧传递，在执行时重新解析；没有把引擎 Actor/Havok 读写搬到通用 worker。审核未发现可重现的栈溢出或本插件 HUD 查询锁反转；这不是对任意外部插件线程调用的保证。

SKSE 的消息派发同步调用 listener，任务队列在游戏任务泵调用线程执行。新版本会在任务中绑定并记录 owner；还需把实际 task 线程与 Player Update/hook 线程对照，不能仅凭 AddTask 接口断言所有引擎回调都在同一线程。混合线程回归模拟先后派发，不覆盖多个生产者并发入队。

## 重击和当前上游模式

用户在比较历史与当前上游后选择当前上游模式：重击费用交回引擎，撤销此前恢复的重击命中/落空比例结算。

- 成本 hook 调用原生计算；仅保留已有的非战斗免耗规则和 actor 独立开关。`bNonCombatStaminaCost=false` 且对应 actor 启用攻击体力功能时，非战斗攻击免费。
- 攻击缓存只登记非 bash、非重击；重击命中、被挡及 `attackStop` 均不追加插件重击扣款。原生重击费用不依赖插件收到 `preHitFrame`，原先“抑制原生但缺少登记”的免费路径已移除。
- 移除 `fMeleeCostHeavyMiss_Percent`、`fMeleeCostHeavyHit_Percent` 及菜单项。旧配置中的两个键按未知键忽略，其他配置继续加载。
- `bBlockedHitRegenStamina` 只控制被挡轻击奖励；当前轻击仍按每次成功命中奖励，并在 `attackStop` 或下一次登记时结算 pending 轻击落空。
- 主机回归覆盖原生函数单次转发与参数保持、缺失 preHitFrame/attackData、玩家/NPC 四开关组合、非战斗免耗、轻击转重击，以及重击命中/被挡/停止不追加结算。该改动未再次启动游戏测试。

## 性能证据与采用范围

主机：i5-13600KF，14 核 / 20 逻辑线程，Windows，MSVC 19.44；stun A/B 使用 Clang 20.1.0、Windows MSVC STL、`-O2`。基线直接提取 Git HEAD 的生产源码，current 提取工作树生产源码。

### stun：32 states，7 组 × 5000 次，中位数

| 操作 | HEAD | 当前 | 每操作分配 |
| --- | ---: | ---: | --- |
| modStun | 1003.10 ns | 17.00 ns | 35 → 0 |
| 一个状态变化的 update | 1492.84 ns | 76.52 ns | 45 → 0 |

复现：`python tools/benchmark/memory/stun_snapshot_ab.py`。输出 `build/perf-memory/baseline.csv`、`delta.csv`。

### scheduler：101 样本，批次放大后的 p50

| 操作 | N | HEAD | 当前 |
| --- | ---: | ---: | ---: |
| schedule | 1 | 17.2 ns | 62.9 ns |
| schedule | 4096 | 18.9 ns | 145.9 ns |
| cancel | 1 | 13.7 ns | 39.8 ns |
| cancel | 4096 | 5743 ns | 94.2 ns |
| due dispatch | 1 | 67.2 ns | 97.7 ns |
| due dispatch | 4096 | 4902 ns | 74.9 ns |
| pending idle | 4096 | 2200 ns/frame | 50 ns/frame |

索引和树节点使创建任务、小规模取消/到期更贵；采用它是为了消除 pending 每帧扫描及密集批次的平方复杂度。不能据此声称所有路径都更快。p95/p99 是批次摊销时间分布，不能当成单次游戏响应分布。

复现：`powershell -File tools/benchmark/run_benchmarks.ps1 -Samples 101`。数据与环境在 `build/perf/scheduler_*_amplified.csv`、`benchmark_metadata.json`。

### 连续数据、SIMD 与多核试验

旋转计算试验验证了生产标量、连续数据、SSE2、AVX2 和持久四 worker pool 的数值一致性。最近一次 N=4096：scalar noinline 6.91、连续生产调用 3.52、SSE2 1.88、AVX2 4.17、worker pool 7.15 ns/op；小批次线程同步代价更突出，重复运行有明显噪声。数据在 `build/perf/rotation_current.csv`。未把实验 SIMD/worker 实现写入生产 hook，未增加 AVX2 运行要求。

上述数据测量 host 容器、锁及模拟输入的 CPU 成本。projectile 三任务对一任务比较只测调度开销。它们不代表 Skyrim 实际命中、碰撞或 ActorValue 引擎延迟；hit/animation/execution 等路径的完整实机 A/B 尚未完成。

## 独立与对抗验证中撤回的报告

- CommonLib 将 Projectile slot 190 声明为 `hkpCollidable*`，不能单凭此判定插件 collector 形参错误。主代理和子代理分别检查 SE 1.5.97 / AE 1.6.1170 的实际 caller/receiver：caller 给 RDX 传栈上 collector 地址，receiver 读取 +0x10 entries、+0x18 count、按 0x40 步长遍历。因此保留 collector ABI。
- `ButtonEvent::Create` 后的 delete 不能仅以没有类级 allocator 宏判为错堆释放；它有引擎虚析构路径，没有获得相反的实际 deleting-destructor 证据，撤回该缺陷报告。
- stun 的无效 handle 删除并未自动丢失 graph 恢复记账；OwnedEffects 另按 FormID 保留 ownership，不能凭 handler map 擦除判为清理丢失。
- animationFlushing 为 owner-only；worker 在 `ready()` 短路后走队列，不能以其不是 atomic 直接判定 data race。生命周期 owner 绑定则由实机观察另外修正。
- `kPostLoadGame` 文档的 “data: bool” 不是 `bool*`。实际 [SKSE 发送点](https://github.com/ianpatt/skse64/blob/master/skse64/Hooks_SaveLoad.cpp#L48-L49) 将结果转换为 `(void*)result`；当前 `nullptr` / 非空判断正确，撤回要求解引用 bool 的报告。

所有自定义 E8 callsite 重新检查：SE 1.5.97 为 6/6，AE 1.6.1170 为 5/5。该检查证明位置/指令及目标，不证明所有运行时交互。没有验证 1.7.99+。

## 构建与回归

- 切回原生重击后的常规 Release DLL 编译成功，Trace=OFF、COPY_OUTPUT=OFF；Release CTest 16/16，1.92 秒。
- 切回原生重击后的 MSVC AddressSanitizer host CTest 16/16，3.36 秒；ASan runtime 路径加入 PATH。ASan 没有注入游戏进程。
- suites：config、runtime、block、execution、cleanup、message、stun、counter、apply、combat_flow、utils、heavy、projectile、self_test_trigger、trace_0、trace_1。
- 特征测试提取真实生产函数体，使用 fake engine types；runtime 编译实际生产 `.cpp`。不把这些 host 测试说成实机回调、ABI 或所有线程调度的证明。
- Trace 默认关闭，关闭宏连参数求值也消除；启用版本用固定 65536-entry ring、QPC/thread/session/span、溢出计数，锁外落盘。trace_0 验证零参数副作用，trace_1 验证并发、span、overflow 和 CSV。

## 本轮实际游戏观察和部署状态

直接通过 `E:/st/steamapps/common/Skyrim Special Edition/skse64_loader.exe` 启动，PID 33404，用户确认到达主菜单。没有继续使用 MO2，没有修改 D 盘原游戏。

E 游戏部署了本次较早的 trace-on DLL、ESP、Killmoves.ini、RaceMapping/Vanilla.ini；四个目标原先均不存在。插件启用文件也已备份，清单为 `build/direct-game-backup-20261106T040447Z/manifest.json`。游戏日志确认插件加载、表单/配置读取及 hook 安装；缺少 Menu Framework、TrueHUD、Precision，因此本轮没有这些集成的实机覆盖。

第一轮只产生 `build/trace/combat_trace_33404_6424114985461_1.csv`，其中 events=0，未产生 combat_self_test.csv。这不算自动自测通过，也没有战斗 trace。

第一次桌面测试在用户按物理 Escape 后停止。随后用户明确授权再次命令行重启，并要求等待其通知主菜单、期间不使用桌面控制。根据该新指令，诊断版本取消自动启动，改为监听绝对路径 `E:/source/repos/valhallaCombat/build/trace/combat_self_test.request`；收到文件后只向 SKSE queue 投递自测，文件监听线程不调用引擎。新增回归验证无请求时不执行、只投递一次、实际测试与 flush 由任务 owner 执行。

12:29 已备份并替换 E 游戏中的 DLL，经直接 SKSE 启动了新进程 PID 29988。当时诊断 DLL SHA256 为 `EA0835498F4D72E10F192D9C3400388D751009ECA65A8116454F76A89EB98182`，副本在 `build/artifacts/trace/ValhallaCombat.dll`。前一游戏 DLL 备份为 `build/artifacts/trace/ValhallaCombat.before-manual-trigger.dll`。

随后切回原生重击的常规版本在 `build/src/Release/ValhallaCombat.dll`，交付副本在 `build/artifacts/release/ValhallaCombat.dll`。该版本只做了编译和主机回归，未部署到游戏目录，也未再次启动游戏；以下主菜单结果来自前述诊断版本。旧 `build/packaging/*.zip` 未重打包。

用户确认进入后写入 request 文件，DLL 已消费；自测于 12:32:26.942–12:32:26.946 在线程 55632 实际执行，与此进程初始化 owner 的线程相同。结果文件已生成并逐项核对：

- `build/trace/combat_self_test_summary.txt`：`core_checks=passed`，96 个 timed callbacks 执行，cancelled callbacks=0；垂直正/负及一般旋转矩阵均通过；玩家 handle 解析、ActorValueOwner 与 stamina 读值有效且有限。
- `build/trace/combat_self_test.csv`：6 组 scheduler batch（每组16次），237.5–312.5 ns/迭代；12 组旋转矩阵 batch（每组4096次），7.47–8.13 ns/调用，含 noinline wrapper 和 volatile sink。两组 form lookup 均16/16找到；16次玩家 handle 解析成功；64次 stamina 读取有限。这是在游戏进程中的主菜单测量，样本很少，无战斗负载，不能用于战斗延迟分位数结论。
- `build/trace/combat_trace_29988_6439367964141_1.csv` 已落盘；主菜单没有战斗事件，不据空 trace 声称战斗覆盖。CSV 明确将 melee_hit、projectile_collision 标记为 `not_run_main_menu`。

这完成了本次主菜单手动触发及结果读取。没有在该轮使用桌面控制，未进入战斗场景。Player Update/战斗 hook 的实际线程及完整战斗路径仍待另行采集。

## 未覆盖的实机场景（本轮已结束）

本轮按用户要求在主菜单测试后结束。线程与加载生命周期的可复用发现已整理到 [跨项目笔记](/E:/source/repos/SKSE-Threading-Loading-Notes.md)。以下场景留作后续项目参考：

1. 在测试场景采集轻击、原生重击、非战斗免耗、worker animation 与 Precision/原生 contact 的完整事件轨迹和 stamina 前后值。重击已切回原生扣款；轻击登记与落空结算仍需要动画事件。
2. 采集格挡/弹反 bursts、execution 接受/拒绝、保存/加载/菜单转换、卸载再加载与慢动作恢复；测真正引擎调用的 p50/p95/p99，再决定其他连续存储、SIMD 或并行计算优化。

当前结果是已实现并回归验证的修复和测量基础，尚未完成“全部热路径实机延迟验证”。

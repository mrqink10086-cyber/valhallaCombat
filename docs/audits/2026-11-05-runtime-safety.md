# Valhalla Combat 运行时安全与发布说明审查

日期：2026-11-05。源码基线：`364ff1740d0dfbce24bf8d8139c6635408f2e69f`，对应 `v0.2.0-NG`。

本报告合并两轮审查：时序、堆栈、线程、指针和执行阶段审查，以及对发布说明和实际发布包的交叉检查。只记录问题与修复方向，没有修改插件实现。处决用户反馈另见 [处决诊断](2026-11-05-execution-diagnosis.md)；后续 [全功能执行时态审查](2026-11-05-feature-phase-audit.md) 区分玩法、逻辑与真正时序，补充格挡公式、耗尽、冲撞及设置切换的受控复现。

完整实施安排见 [全面修复与 SKSE Menu Framework 迁移计划](2026-11-05-remediation-plan.md)。

本文保留修复前结论；当前代码的修复与验证状态见 [实施报告](2026-11-05-implementation.md)，其中以实际二进制证据更新投射物 ABI 待证项。

## 证据与等级

- **确定**：源码直接形成错误路径，或发布配置与读取逻辑可以直接对照。
- **条件性**：缺陷可确认，但发生条件仍取决于回调并发、对象卸载或生成失败等运行状态。
- **待运行时核验**：发现依赖契约差异，尚未用目标游戏反汇编或现场轨迹确认。
- **P1**：可能崩溃、产生未定义行为或破坏引擎状态；**P2**：行为、时序或同步缺陷；**P3**：低影响问题。

未运行游戏，未完成 SKSE 插件编译；本地 CommonLib 构建环境未配置。没有将源码审查等同于游戏内复现。

## A. 运行时安全问题

### A01 / P1 / 确定：箭矢格挡读取未初始化的栈变量

位置：`src/bin/blockHandler.cpp:258`、`:266`、`:277`、`:284`。

外层 `float cost;` 未初始化，箭矢分支重新声明 `float cost = 0;`，只为内层变量赋值。后续格挡函数读取外层不确定值，属于未定义行为；普通非强制箭矢格挡即可到达。

修复方向：初始化唯一的外层变量，删除分支内的同名声明。

### A02 / P1 / 条件性：处决任务携带裸指针，执行时没有重新验证

位置：`src/bin/executionHandler.cpp:108`、`:123`、`:138`、`:148`、`:154`。

执行者和目标以裸 `Actor*` 穿过分离线程和游戏任务队列。排队前的存活、3D、骑乘和 killmove 检查无法保证任务执行时仍成立；任务直接访问 `currentProcess` 并播放配对动作。读档、卸载、死亡或另一请求改变状态时，存在失效对象访问或动作拒绝风险。

修复方向：捕获 ActorHandle，执行时解析并保留 NiPointer，检查双方状态和当前会话；按角色记录待执行请求，避免重叠请求。默认实际调用次数为 1，不能将旧注释理解为有十次重试。

### A03 / P1 / 确定：空处决列表仍被随机选择、解引用

位置：`src/bin/executionHandler.cpp:119`—`:124`；`src/include/Utils.h:96`—`:100`。

空列表只输出错误日志，随后构造上界为 -1 的随机区间并解引用结果。动作表单缺失或配置加载不到可用 idle 时可到达此路径。

修复方向：空列表立即失败并向调用者返回原因，动作数据加载结束时检查实际使用的列表。

### A04 / P1 / 条件性：处决线程共享无锁随机数生成器

位置：`src/include/Utils.h:103`—`:107`。

同一迭代器类型的调用共享可变静态 `mt19937`，多次处决请求各自启动工作线程，可并发修改生成器，形成 C++ 数据竞争。

修复方向：在统一线程选择动作，或明确保护生成器；无需为一次排队额外启动工作线程。

### A05 / P1 / 条件性：Elden Counter 后台线程直接修改 Actor

位置：`src/bin/blockHandler.cpp:714`—`:728`。

添加触发法术后启动分离线程，睡眠结束直接对裸 Actor 指针调用 `RemoveSpell`。非空检查不能证明指针仍有效，也没有将引擎修改回送到游戏线程。

修复方向：主线程定时调度；执行时解析句柄并检查会话和本次法术请求是否仍有效。

### A06 / P1 / 确定的覆盖错误、条件性的并发风险：慢时间恢复

位置：`src/include/Utils.h:115`—`:124`；`src/include/offsets.h:29`—`:35`。

后台睡眠结束直接写全局时间倍率为 1。A、B 连续触发时，A 的恢复会截断 B；已有倍率被固定值覆盖。暂停和读档不停止墙钟睡眠，工作线程与引擎访问全局量之间没有同步。

修复方向：统一管理慢时间期限、恢复倍率和会话，在主线程执行状态变更；明确使用游戏时间还是现实时间。

### A07 / P1 / 条件性：格挡特效生成失败后解引用空指针

位置：`src/bin/blockHandler.cpp:622`—`:637`。

`PlaceAtMe` 结果未经检查即调用 `MoveToNode` 和 `SetDelete`。特效表单缺失或生成失败会进入空指针路径。

修复方向：检查表单与生成结果，失败时退出本次特效播放。

### A08 / P2 / 确定：旧的延迟解防会终止新格挡

位置：`src/bin/Hooks.cpp:213`—`:229`。

`|| player->AsActorState()->IsStaggered()` 位于非空及按键状态保护之外；任务结尾无条件发送 `blockStop`。松键后立即重新按住，旧任务仍可结束新格挡；非空保护也不完整。

修复方向：修正布尔分组，统一保护所有玩家访问；用请求代次取消旧解防操作。

### A09 / P2 / 确定：窗口去重跳过真实的重新按下

位置：`src/bin/blockHandler.cpp:117`—`:151`。

已有 timed-block 窗口时提前返回，而 `isBlockButtonPressed = true` 位于后面。窗口内 down → up → down 会保留 held=false，进一步影响延迟解防。0.2.0 的重复回调修复没有区分重复通知和真实的新按键周期。

修复方向：先维护物理按住状态，再对窗口触发去重。

### A10 / P2 / 确定：加载边界缺少完整重置和旧任务失效机制

位置：`src/bin/main.cpp:58`—`:79`；`src/bin/attackHandler.cpp`；各分离线程入口。

PostLoadGame 只清理部分 debuff/stun 和游戏设置，没有统一清理攻击缓存、格挡/冲撞计时，也没有取消旧的处决、解防、移除法术和慢时间恢复任务。没有 PreLoadGame / NewGame 对应的统一生命周期处理。

修复方向：集中管理会话代次和战斗状态，在加载边界失效旧任务，按每个状态的持有责任复位。

### A11 / P2 / 条件性：stun 容器与对象没有完整同步协议

位置：`src/bin/stunHandler.cpp:46`—`:67`、`:89`—`:95`、`:237`—`:244`、`:323`—`:334`。

查找释放共享锁后才插入；重复插入忽略 insert 结果，返回未进入 map 的新对象。容器锁外通过 shared_ptr 修改内部数值；共享锁下 refill 也会写对象；`getStunBrokenActors()` 无锁复制集合。shared_ptr 只保证数据对象生命周期，不保证数据访问互斥。

修复方向：统一线程所有权，或覆盖完整的数据事务，插入时返回实际存入 map 的对象。当前未证明所有相关回调实际并发，不能宣称每次均产生竞争或已经死锁。

### A12 / P2 / 条件性：攻击缓存迭代器在锁外取得、跨引擎调用后删除

位置：`src/bin/attackHandler.cpp:46`—`:55`。

无锁 find 后调用引擎 staminaLightMiss，之后才加锁 erase(iterator)。并发或重入修改集合时迭代器可能失效。缓存使用裸 Actor 地址，且没有加载边界清理。

修复方向：在锁内完成判定和移除，再在锁外执行引擎副作用；处理缓存生命周期。

### A13 / P2 / 条件性：stun 数据的无效句柄与 Actor 所有权

位置：`src/include/stunHandler.h:18`、`:33`—`:41`、`:100`—`:101`。

句柄解析失败只记日志，仍调用 calculateMaxStun；长期保存 `_actor` 裸指针。`handle.get().get()` 的临时 NiPointer 在语句结束释放，不构成长期引用。

修复方向：拒绝无效构造，保存句柄；使用时解析并在本次调用期间保留引用。不能仅据裸指针就断言所有同步调用都已发生 UAF。

### A14 / P2 / 确定：debuff 闪烁线程启动、停止状态错误

位置：`src/bin/debuffHandler.cpp:72`—`:89`、`:123`—`:132`。

先启动线程后设置运行标志，新线程可能立即退出；关闭 UI 提示后 revertDebuffUI 提前返回，跳过停止标志。睡眠后没有重新确认本次运行，快速停止/启动可能留下重复工作线程。

修复方向：先建立运行状态，停止不依赖 UI 设置；统一管理工作生命周期。

### A15 / P2 / 确定：体力自然恢复遗漏耗尽图变量清理

位置：`src/bin/debuffHandler.cpp:31`—`:39`、`:92`—`:109`。

自然恢复会移除跟踪记录和 perk，却没有复位 gv_bExhausted。之后 stopDebuff 因记录不存在直接返回，无法补清理。

修复方向：统一完整的 debuff 退出流程。

### A16 / P2 / 条件性：初始化输入错误可使初始化异常退出

位置：`src/bin/blockHandler.cpp:738`；`src/bin/data.cpp:273`—`:279`。

`std::stof(ini.GetValue(...))` 没有处理缺键、非数字和范围错误；对动作种族配置目录直接构造 directory_iterator，目录缺失会抛出 filesystem_error。

修复方向：对实际依赖的配置输入和目录存在性作明确处理，避免半初始化状态。

### A17 / P3 / 确定：加载统计变量未初始化

位置：`src/include/Utils.h:530`、`:559`、`:569`、`:592` 等。

`_loadedForms`、`_loadedSettings` 没有初始化即递增、记录，属于未定义行为。主要可见影响是统计日志错误。将计数器初始化为 0。

## B. 发布说明、依赖与实际资产

### B01 / P1 / 待运行时核验：投射物 Hook 签名与声明依赖不一致

位置：`src/include/Hooks.h:204`—`:214`；`src/bin/Hooks.cpp:155`—`:176`。

源码把 0xBE 槽声明为 `void(Projectile*, hkpAllCdPointCollector*)`。发布说明指定的 CommonLibSSE-NG v11.0.0 声明同一槽为 `bool Projectile::HandleHits(hkpCollidable*)`；ArrowProjectile / MissileProjectile 继承该方法。差异涉及返回值和第二参数语义。若头文件契约对应实际游戏函数，则 Hook 会把错误对象当 collector 读取，并且没有保留 bool 返回值。

这是一项必须核对的 ABI 差异，尚不能直接断言已经造成栈破坏。Windows x64 下参数数目相同也不代表对象解释与返回约定正确。应对实际支持版本的 0xBE 函数和调用者反汇编，确定真实签名。

### B02 / P2 / 确定：MCM 默认值与 DLL 实际读取不一致

位置：`src/bin/settings.cpp:103`；`src/include/settings.h:77`；包内 `MCM/Config/ValhallaCombat/settings.ini:13`。

DLL 仅读用户覆盖文件 `Data/MCM/Settings/ValhallaCombat.ini`，不加载包内默认文件。MCM Helper 自己先读 defaults、再读用户文件，并按修改项单键持久化，不会自动输出整份 defaults。因此未修改的键可能不存在于用户文件中。

明确例子：包内 bBlockedHitRegenStamina=1，C++ 默认 false。干净安装时 UI 与 DLL 可显示/执行不同值。修复方向：DLL 按默认文件→用户覆盖的顺序读取，或通过一致的设置接口读取最终值。

### B03 / P2 / 确定：MCM 的完美格挡时间滑块没有消费者

位置：包内 `config.json:379`、`settings.ini:41`；`src/bin/settings.cpp:150`—`:151`；`src/bin/blockHandler.cpp:435`—`:443`。

UI 暴露 fPerfectBlockWindow，默认 0.15；DLL 没有读取该键。实际用 isPcHot、pcPerfectBlockCountdown 和 uPerfectBlockAccum 决定完美格挡，属于计次机制。滑块修改无效。

修复方向：让配置、UI 和实现使用同一个机制。

### B04 / P2 / 确定：第一个有效接触对象可遮蔽后续格挡目标

位置：`src/bin/Hooks.cpp:124`—`:153`，特别是 `:144`、`:149`。

遍历 collector 时找到首个有效 Actor 就 return processProjectileBlock；结果为 false 也结束遍历。首个 NPC 无法格挡时，后续玩家或强制格挡目标不会被检查。应仅在成功消费碰撞时返回，失败则继续。

### B05 / P2 / 条件性：反射后所有者更新，但物理过滤组更新被跳过

位置：`src/bin/blockHandler.cpp:324`；`src/include/Utils.h:310`—`:315`。

延期反射任务向 resetProjectileOwner 传入 nullptr collidable；帮助函数更新 shooter/cause 后提前返回，跳过 collision filter 更新。逻辑所有者与物理过滤信息可能不一致，具体对原射手或新射手的影响需实测。

修复方向：在安全执行阶段重新取得有效碰撞对象，完成所有权与过滤状态的一致更新，不延长旧 collector 指针的生命周期。

## C. 执行阶段与已排除的误报

| 阶段 | 当前行为 | 审查结论 |
| --- | --- | --- |
| DataLoaded | 先安装 Hook，再读取设置、注册事件和加载数据 | 存在初始化先后关系；未证明此阶段一定有回调并发进入 |
| 玩家 Update | mod 更新先于原 Update；暂停时跳过 handler | 常规计时与墙钟睡眠不一致 |
| 投射物碰撞 | 检查接触并同步扣值、经验、动画、特效；反射/销毁改为排队 | 延期改进只覆盖投射物修改，不能推论所有副作用都已转移 |
| 输入处决 | 选择当前破防集合中的目标，排队后播放动作 | 目标选择、队列消费、动作接受是不同阶段 |
| TrueHUD UI 更新 | 调用注册的 stun getters | 需运行轨迹确认与更新/命中线程是否重叠 |
| 加载存档/新游戏 | 无统一任务代次 | 旧任务可污染新状态 |

已核实且不作为缺陷：

- ProcessButton 在 v11 的 VersionShift 契约中确为旧版 0x04、1.7.99+ 0x06；先前旧上游头文件不适合据以判错。
- 发布 DLL 确实导出 SKSEPlugin_Query，版本元数据包含 AddressLibraryV5；不报告缺失标志导致拒载。
- PlayerUpdate 0xAD 与所声明依赖一致；不能将输入槽新增误推成所有虚表都要 +2。
- TrueHUD Flash 内部通过 AddHUDTask 排队，不直接判为工作线程操作 GFx。
- Precision PreHit 经 PendingHit 更新路径派发，不能等同于任意 Havok 工作线程。
- PlayerControlsData 是 PlayerControls 的内嵌成员；不能把延迟解防的 a_data 直接定性为已经失效的栈地址。
- synthetic ButtonEvent 是自行创建的事件；没有证据证明 delete 与引擎析构器分配方式不匹配。
- 单例中的 bool 具有静态零初始化；不报告为未初始化。
- 固定 ActorState 偏移的旧 stamina hook 未启用；AE GetWantBlock 的 0 地址会跳过，发布说明也披露该功能为 SE-only。
- 没有证实栈溢出、递归爆栈或锁循环死锁；引擎调用位于锁内仅保留为重入核查点。

## D. 版本和发布包核验

- 发布页明确称基于原版 1.3.3，0.2.0 主要修正输入虚表、MCM 路径/状态、死亡判断和重复格挡通知，没有声明修复处决流程。
- 实际发布 ZIP：`ValhallaCombat-0.2.0-NG.zip`，SHA256 `02B77372A6828A90750A47F515C6A9377339FAD44F471C02F41A60A438F22A5F`，与发布说明一致。
- 发布 DLL 818688 字节；SKSEPlugin_Version 的 versionIndependenceEx=3，包含 NoStructUse 和 AddressLibraryV5；有 Query / Load / Version 导出。
- 作者仅披露 AE 1.6.1170 实测；1.7.99+ 输入槽来自依赖表。宽泛的 1.7.x 支持声明不等同于全部调用点已获运行时验证。

## 参考资料

- [发布说明 v0.2.0-NG](https://github.com/blink-blink/valhallaCombat/releases/tag/v0.2.0-NG)
- [CommonLib v11 PlayerInputHandler](https://github.com/alandtse/CommonLibSSE-NG/blob/v11.0.0/include/RE/P/PlayerInputHandler.h)
- [CommonLib v11 Projectile](https://github.com/alandtse/CommonLibSSE-NG/blob/v11.0.0/include/RE/P/Projectile.h)
- [CommonLib v11 SKSE 接口及元数据](https://github.com/alandtse/CommonLibSSE-NG/blob/v11.0.0/include/SKSE/Interfaces.h)
- [MCM Helper 设置默认值与单键持久化](https://github.com/Exit-9B/MCM-Helper/blob/main/src/SettingStore.cpp)
- [TrueHUD HUDHandler 的 Flash 排队](https://github.com/ersh1/TrueHUD/blob/master/src/HUDHandler.cpp)
- [TrueHUD UI 读取特殊资源值](https://github.com/ersh1/TrueHUD/blob/master/src/Widgets/InfoBarBase.cpp)
- [Precision PendingHit](https://github.com/ersh1/Precision/blob/main/src/PendingHit.cpp)

# Valhalla Combat 修复实施与验证

日期：2026-11-05。修复前基线：`364ff1740d0dfbce24bf8d8139c6635408f2e69f`。跨工作包共享接口采用一次集成提交，回归结果仍按工作包分别记录。

[修复计划](2026-11-05-remediation-plan.md)中的代码、配置迁移、原生菜单接入及完整资产打包已经实现。Release 构建和九组主机回归通过，SE 1.5.97 / AE 1.6.1170 二进制取证完成。游戏尚未部署或运行，W7 的实机验收仍待完成。

## 工作包与问题关闭依据

| 工作包 | 已实现内容 | 验证与边界 |
| --- | --- | --- |
| W0 | 固定 NG 与 vcpkg 基线；修正投射物完整函数类型；安装前检查自定义 call 指令；空值、未初始化值与空动作保护 | Release `/W4 /WX`；两个游戏二进制的投射物 ABI 与 11 个 callsite 取证；没有以版本白名单代替多目标支持 |
| W1 | `CombatRuntime` 所有者线程、会话代次、暂停计时、可取消任务、加载边界；导出 API 保持 V1/V2，写命令交接、只读快照；效果序列化账目 | runtime/message/cleanup 回归；成功读档消息按指针编码布尔值处理，失败读档不启动会话；跨存档的引擎实际保留行为待实机 |
| W2 | 格挡公式、零成本与恰好耗尽、按键周期、旧松开取消；攻击缓存句柄与先删后结算；投射物多接触扫描、待处理身份、重新取得物理对象并更新 owner/filter | block/counter/apply 回归；原生与 Precision 的事件顺序、接触去重和连续反射待实机轨迹 |
| W3 | stun 句柄值状态、重入事务、发布快照；耗尽/倒地的创建所有权与失败清理重试；重新获取效果覆盖旧清理；EC 实例代次；慢时间重叠、原值恢复与外部修改让出 | stun/cleanup/counter/runtime 回归；外部调用不保留正在遍历的状态迭代器；引擎图变量和第三方效果组合待实机 |
| W4 | 可用候选后备选择、空 idle 拒绝、单线程 RNG、句柄请求、消费时复查、pending 去重与实际播放 bool 回报 | execution 回归覆盖失效队列与同步重入；配对动画是否被游戏接受待实机 |
| W5 | 66 项活动设置统一定义；schema 1、旧配置优先级与备份、原子保存、版本化草稿与生效回执；切换清理攻击缓存、按键、EC、慢时间和自有 HUD 颜色 | config/apply 回归覆盖迁移、坏值、保存失败、过期草稿和 HUD 先还原后释放控制 |
| W6 | 固定 Menu Framework 公共 SDK；API/导出预检；六个中英页面、草稿操作和键鼠/手柄绑定采集；移除 MCM 依赖与菜单专用资产 | DLL 编译通过；中文字体、导航、输入消费、关闭菜单与暂停 Apply 仍需实机 |
| W7 | 修复前证据固定基线；九组回归；完整 Data 根安装包、资产迁移校验与升级说明 | 主机测试和打包通过；游戏矩阵未执行，1.7.99+ 没有二进制或运行证据 |

原审查 A01–A17、B02–B04、T01–T07、L01–L06 的代码修复对应上表 W1–W6。B01 的投射物类型问题已有下面的二进制结论；B05 的物理过滤更新已实现，实际反射效果仍待实机。V 类攻击事件归属、双碰撞入口、实际线程/阶段及链式 Hook 的兼容行为保留为实机验收项。

既定玩法保持：普攻眩晕默认 0，最大眩晕值按永久生命与耐力平均计算，约 3 秒等待加 7 秒恢复，后续近战命中触发自动处决，手动处决仅按下触发，奖励继续按命中结算。未增加同击处决、输入缓冲或玩家被处决；死控件由真实累计次数等活动设置替代。

复审发现并修复了读档结果错误解引用、耗尽清理丢失重试、stun 重入迭代器、旧 EC 期限删除新实例、旧清理删除重新获取效果、关闭攻击开关遗留缓存，以及 TrueHUD 释放前未还原颜色。复核这些改动后没有剩余已确认代码缺陷；这不替代上述未执行的游戏验收。

## 构建基线

- CommonLibSSE-NG：`v11.0.0`，提交 `94faaed0c60eddd8347767f2d4d29a97c93bde8c`。
- vcpkg baseline：`ee12231b20c95013c6638d845d04c91559a1d1ff`，triplet `x64-windows-static-md`。
- Visual Studio 2022 / v143，C++20，SE/AE 同时开启、VR 关闭、`COPY_OUTPUT=OFF`。
- Menu Framework 公共 SDK：Framework 3.8 / API 1，SHA256 `03a39ccef309d234b0575e307f741907a18a389dc4a9e398730a1823db8c5153`。SDK 与 GPL-3.0 许可随源码保留，外部框架仓库未修改。
- Release DLL：`build/src/Release/ValhallaCombat.dll`，940544 字节，SHA256 `241b15e998b337194ad1dfba8f31cd3bbf2de49345a1c8d2515661dd45ac8637`。

在仓库根目录使用已经准备好的 vcpkg 与固定依赖构建：

```powershell
git clone --branch v11.0.0 https://github.com/alandtse/CommonLibSSE-NG.git .deps/CommonLibSSE-NG
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -T v143 -DCMAKE_TOOLCHAIN_FILE=E:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static-md -DCOPY_OUTPUT=OFF -DENABLE_SKYRIM_SE=ON -DENABLE_SKYRIM_AE=ON -DENABLE_SKYRIM_VR=OFF "-DCMAKE_CXX_FLAGS=/EHsc /MP /W4 /WX"
cmake --build build --config Release -- /nologo /v:minimal
ctest --test-dir build -C Release --output-on-failure
```

九组测试分别为 config、runtime、block、execution、cleanup、message、stun、counter、apply，恢复普通 Release 后最终运行 **9/9 通过，0.59 秒**。SettingsStore 与 CombatRuntime 直接编译生产源文件；其余模块由 `tests/generate_functions.py` 提取未改写的生产函数体，替身仅提供受控引擎环境。这些测试验证状态与调用边界，不模拟 Skyrim ABI 或动画系统。

同一九组程序使用 MSVC 14.44.35207 AddressSanitizer 编译后 **9/9 通过，0.72 秒**，没有地址检查报告。`dumpbin /dependents` 确认测试程序实际依赖 `clang_rt.asan_dynamic-x86_64.dll`。复现时仅为独立测试启用 ASAN，向本次进程 PATH 加入已安装工具链的 ASAN runtime 目录；发布 DLL 使用普通 Release 构建：

```powershell
$env:CL = "$env:CL /Zi"
cmake --build build --config Release --target config_regression runtime_regression block_regression execution_regression cleanup_regression message_regression stun_regression counter_regression apply_regression -- /nologo /v:minimal /p:EnableASAN=true
$env:PATH = "E:/BuildTools/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64;$env:PATH"
ctest --test-dir build -C Release --output-on-failure
```

该工具链的 ASAN 链接提示建议增加 `/DEBUG` 以改善错误符号；本次检查已启用编译调试信息和地址插桩。首次未加入 ASAN runtime 搜索路径时测试启动失败 `0xc0000135`，修正进程 PATH 后得到上述通过结果。检查结束后已恢复普通 Release 测试程序。

修复前 `docs/audits/probes/execution_probe.py` 与 `feature_probe.py` 从上述基线提交取源码，保留原缺陷复现用途；不能将其“成功复现旧缺陷”的退出码当成修复后验证。

## Projectile ABI 与自定义 callsite

在本机合法安装的可执行文件副本上只读取证；分析使用解包副本，原游戏文件保持不变。签名应为：

```cpp
bool(RE::Projectile*, RE::hkpAllCdPointCollector*)
```

两份二进制都将第二参数从 `RDX` 保存为 `r15`，读取 `[r15+0x18]` 接触数、`[r15+0x10]` 接触数组，并以 `index << 6` 遍历 0x40 字节元素；返回路径明确设置 `AL=0` 或 `AL=1`。这与 collector 结构一致，不能按单个 collidable 读取。NG v11 头文件的第二参数声明不符该实证，当前 Hook 与原函数 Relocation 均使用 collector/bool；拦截成功返回 true，否则传回原函数结果。

| 运行时 | Projectile vtable | 0xBE 实现 | Arrow / Missile |
| --- | --- | --- | --- |
| SE 1.5.97 | `0x14167c888` | `0x1407521f0` | 都继承同一实现 |
| AE 1.6.1170 | `0x1418c4470` | `0x1407ebd10` | 都继承同一实现 |

SE 的 false/true 指令位于 `0x1407525e5` / `0x1407525e9`；AE 对应 `0x1407ec0f6` / `0x1407ec0fa`。这关闭了原报告中参数、返回值待证项，没有据此宣称曾发生实际栈破坏。

下表每一位置均为完整的 5 字节 `E8 rel32` call 指令。安装预检在任何补丁前检查所有活动 callsite 的 opcode，发现不匹配则不启动战斗会话；预检并不自动证明未来版本的语义相同。

| 调用点 | SE Address Library ID + 偏移 → 目标 | AE Address Library ID + 偏移 → 目标 |
| --- | --- | --- |
| stamina | `37650+0x16e → 0x1403bec90` | `38603+0x171 → 0x1404183e0` |
| restore | `37510+0x176 → 0x140620900` | `38452+0xe1 → 0x1406b1e30` |
| melee_hit | `37673+0x3c0 → 0x140626400` | `38627+0x4a8 → 0x1406b7bc0` |
| melee_collision | `37650+0x38b → 0x140628c20` | `38603+0x45a → 0x1406ba870` |
| attack_action | `48139+0x4d7 → 0x14075ff10` | `49170+0x435 → 0x1407f9470` |
| want_block | `37376+0x23 → 0x14071b710` | AE 不安装此补丁 |

PlayerInputHandler 的 ProcessButton 使用 NG 对 1.7.99 前后槽 4/6 的版本化选择，PlayerUpdate 保持槽 0xAD。前述两个二进制不覆盖 1.7.99+，该范围没有填为已实测。

取证输入的 SHA256：

| 输入 | SHA256 |
| --- | --- |
| SE 1.5.97 解包副本 | `de92095a18513fcaffe5a86fd72879d3350c61cdcdb8500b7d31df2bae9579cd` |
| SE Address Library | `1d7530d001139ca58f462ea0210a8055868159057ba8b5ebc624fc5e9c4f5e9a` |
| AE 1.6.1170 解包副本 | `80c1ea737d33c6bfac09b101b8d77ab0f9f6630128c3ed052f9d945bed54e7e4` |
| AE Address Library | `c4093c569a3c83b26587f4b9ea4c55de9ae6e73b84a2af9fb3fbd30e2fe0d452` |

复现（Python 3 + Capstone，使用自己的对应版本二进制与 Address Library）：

```powershell
python docs/audits/probes/projectile_abi.py build/abi/SkyrimSE-1.5.97.exe.unpacked.exe
python docs/audits/probes/projectile_abi.py build/abi/SkyrimSE-1.6.1170.exe.unpacked.exe
python tools/verify_hooks.py build/abi/SkyrimSE-1.5.97.exe.unpacked.exe "E:/Stock Game CK/Data/SKSE/Plugins/version-1-5-97-0.bin" --output build/abi/1.5.97-callsites.json
python tools/verify_hooks.py build/abi/SkyrimSE-1.6.1170.exe.unpacked.exe "E:/st/steamapps/common/Skyrim Special Edition/Data/SKSE/Plugins/versionlib-1-6-1170-0.bin" --output build/abi/1.6.1170-callsites.json
```

## 配置、资产与安装包

设置权威路径是 `Data/SKSE/Plugins/ValhallaCombat/Settings.ini`。无 schema 时按内置默认 → MCM defaults → 原 SKSE INI → MCM 用户覆盖迁移；同路径旧文件备份为 `.legacy.bak`。新 schema 文件不再被旧 MCM 自动覆盖，未来 schema 不降级，保存失败不改变实际生效设置。

资产源为原 `v0.2.0-NG` 发布 ZIP，SHA256 `02b77372a6828a90750a47f515c6a9377339fad44f471c02f41a60a438f22a5f`。`tools/AssetMigration` 用 Mutagen 0.50.0 仅删除 `ValhallaCombat_MCM_Quest`（FormID `000D62`），对另 249 个玩法记录及 FormKeys 逐一校验一致。输出 ESP 的 masters 为 Skyrim、Update、Dawnguard、Dragonborn；无 MCMHelper/SkyUI 依赖。

完整 ZIP 以 Data 为根，包含新 DLL、改写 ESP、玩法动作/种族映射/音效等原资产、SDK 许可与安装说明；移除已检查的旧 MCM、Interface 与菜单 Scripts，不携带可覆盖用户配置的默认 INI。打包脚本检查资产源哈希、必需文件、路径与重复文件名，并输出 SHA256 文件。游戏可执行文件、分析副本、构建依赖、调试工具与框架运行时 DLL 均不入包。

```powershell
python tools/package.py --assets C:/Users/15219/AppData/Local/Temp/valhalla-release-audit-20261105/ValhallaCombat-0.2.0-NG.zip --dll build/src/Release/ValhallaCombat.dll --output build/packaging/ValhallaCombat-1.0.0-MenuFramework.zip
```

也可配置 `VALHALLA_UPSTREAM_ASSETS` 后构建 `package_mod` 目标。安装与旧配置保留方式见 [安装说明](../INSTALL-MenuFramework.md)。已有存档中的旧 Papyrus 实例不会由替换资产包重写。

本次最终包：`build/packaging/ValhallaCombat-1.0.0-MenuFramework.zip`，4426864 字节、33 个文件，SHA256 `bad0bad4623dd31d333034c10d99ffb9e01ea98b443740cc716d2940e91dfbe6`。ZIP CRC 全部通过，包内 DLL 与上述最终 Release DLL 逐字节一致；仅包含 Killmoves 与 Vanilla RaceMapping 两份 INI，没有覆盖新权威配置的 Settings.ini。旁附同名 `.zip.sha256` 校验文件。

## 剩余实机验收

| 验收项 | 当前证据 | 待完成操作 |
| --- | --- | --- |
| SE/AE 自定义 Hook | SE 1.5.97 与 AE 1.6.1170 静态取证 | 启动同一 DLL，验证 Hook 链、线程和锁阶段；补充 1.7.99+ 布局边界 |
| 原生菜单 | SDK/API 接入与编译 | 字体、中英显示、手柄导航、绑定输入消费、暂停 Apply、关闭菜单释放输入 |
| 投射物与命中 | 主机决策回归、collector ABI | 原生/Precision 接触身份与顺序，多接触、重复反射、owner/filter 一致性 |
| 攻击与处决 | 句柄队列、失效状态与重入回归 | preHitFrame/hit/attackStop 实际周期、配对动画接受与拒绝、不同种族武器 |
| 持久效果 | 所有权、失败重试、实例代次回归 | 保存后重启/读档、新游戏/主菜单往返、卸载重载及第三方同名效果 |
| 可选集成 | API 获取与退出路径实现 | 无集成、TrueHUD、Precision、二者并用、Elden Counter 的实机组合 |

这些项需要运行游戏才能取得结论；当前安装包是完成代码修复与主机验证的待实机验收构建。

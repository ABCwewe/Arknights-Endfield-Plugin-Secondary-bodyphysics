# Current Runtime Baseline

更新日期：2026-08-23
范围：当前开发工作树；旧 Phase 0 hash 和机器特定路径不再代表现行实现。

## 1. 目录与组件

```text
<game root>/
├─ d3dcompiler_47.dll          proxy loader
├─ vulkan-1.dll                proxy loader
├─ plugin/
│  └─ sbm.dll                  Secondary Motion Runtime
└─ SecondaryMotion/
   ├─ data/characters.default.json
   ├─ presets/<name>.json
   ├─ runtime/config.json
   ├─ runtime/runtime_status.json
   ├─ runtime/developer_command.json
   ├─ developer/
   └─ logs/
```

Runtime 从自身 `plugin/sbm.dll` 路径推导游戏根和 `SecondaryMotion` 数据根，不依赖进程工作目录或机器特定安装盘符。

Manager 是单套 WPF 业务代码，通过 EN/ZH `ResourceDictionary` 本地化。正式 EN/ZH 包只用 `default_lang.txt` 指定默认语言，不维护独立 `Manager_CH` 代码副本。

## 2. Hook 与生命周期

- REQUIRED：`AnimatorMono.PreLateTick`。
- OPTIONAL：`NPCCPUAnimator.LateTick`、`ScriptAnimationJobSyncMono.CalcLayerMainStream`，用于验证后的幂等 replay。
- Runtime startup 完成后，worker 执行 `il2cpp_thread_detach`，之后只运行 Win32/CRT/纯逻辑服务循环。
- Unity/IL2CPP 对象访问留在主线程 hook；worker 只做配置轮询、命令轮询、status、marker cache 和 diagnostics arm。
- `AnimatorMono.PreLateTick` 采用 Unity frame dedup：每个 Unity frame 只允许第一次 callback 完成 compute/write，后续 callback 返回。

frame dedup 已从根源消除“同帧多 Animator callback 重复叠加旋转”。现行代码没有 party/callback-count compensation，也没有相关配置。

## 3. 角色识别与骨骼

- 主控角色链按 `entity_refresh_interval_ms` 刷新；默认 500 ms。
- GameObject 名规范化为稳定 `chr_id`，剥离 `_postmodel`、`(Clone)`、`#数字` 等运行时后缀。
- 角色必须存在于 `data/characters.default.json` 且 profile enabled，未配置角色 fail closed。
- 显式 `bones.right/left` 优先；两侧必须同时为空或同时存在。
- 显式骨骼失败时，只有 `allow_fallback_candidates=true` 才尝试候选表。
- axis 可由 DB/preset 显式指定；未指定时按骨骼家族选择 Z 或 Y。
- 视觉幅度完全由每角色、每 gait 的 Up/Down 参数决定。现行代码没有 `bone_scale`、`amplitude_scale` 或骨型统一倍率。

## 4. gait 分类

分类优先级：

```text
当前 CharacterProfile.animation_rules
→ 通用 locomotion whitelist
→ GaitNone
```

正式 gait：

```text
idle / walk / run / sprint / zipline
```

规则：

- 通用 whitelist 只接受明确 locomotion clip 及明确 locomotion transition；skill、attack、battle、special、未知 clip 默认排除。
- 角色专属 `animation_rules` 可将真实 special/dash 移动 clip 映射到 `walk/run/sprint/zipline`。
- 规则最多 16 条；超过限制时整组拒绝并写配置日志。
- 多条角色规则同时命中时，最长 `contains` 片段优先。
- Unity crossfade 期间只要存在任一已接受 locomotion clip，就继续使用 locomotion gait。
- 不使用 Animator clip weight 选择“最高权重 clip”。
- clip 规则只在 DB 加载/热重载时解析；20 Hz gait sample 只做内存字符串匹配。

完整分类边界见 `GAIT_WHITELIST_UPGRADE.md`。

## 5. Synthetic motion

- 每个 gait 有独立 `amplitude_deg`、`amplitude_down_deg`、`frequency_hz`。
- `amplitude_down_deg=0` 表示对称，即 Down 使用 Up；非零时上下半周期分别使用 Up/Down。
- Up 是首半周期上摆，Down 是次半周期下摆；Main/Characters 两页使用相同绑定。
- amplitude/frequency 由 envelope 平滑，相位连续积分。
- `transitionToIdle` 使用单独 release tau。
- 最终 synthetic target 只在 write-eligible locomotion gait 写回。

安全写门控：

- `GaitNone`：envelope 可继续向零衰减，但不 Compose、不 SetLocalRotation、不 replay。
- Jump disabled：清除 synthetic target，不 Compose、不 SetLocalRotation、不 replay。
- 全局 `enabled=false`、角色 disabled、mode off 或 profile/bone 缺失：不执行自定义写回。

`amplify_native` 保留 quaternion delta 幂放大：`base * (inverse(base) * native)^K`。

## 6. Replay

PreLateTick 是唯一 compute 点。LateTick/SyncCalc 不重新计算，只在验证窗口内重放已缓存 target：

```text
compute once → cache target → optional hook verifies/replays same target
```

角色切换、profile/bone 变化、非 locomotion 或 Jump-disabled 路径会使旧 target 失效，避免跨角色和跨状态 replay。

## 7. 配置与热重载

- `runtime/config.json.revision` 是 hot-apply 触发器。
- worker 低频读取配置；revision 变化时重新构建完整 immutable snapshot。
- startup 和 hot reload 共用同一 loader + final validator。
- 新 snapshot 全部通过后才 atomic swap；失败时保留 last-known-good snapshot。
- 成功后 `runtime_status.applied_revision` ACK；失败原因写 `[CFG]` 日志，并将摘要写入 `runtime_status.error`。
- 动画 callback 内没有文件 I/O、JSON 解析、Sleep 或重复 Animator 扫描。

## 8. Manager Apply 与安装

Apply 顺序：

```text
写工具目录 preset
→ SyncMirror 到游戏 SecondaryMotion/presets
→ runtime/config.json revision + 1
→ 等待 runtime_status.applied_revision ACK
```

首次安装：

- 选择游戏根；
- 创建 `SecondaryMotion/{data,presets,runtime,developer,logs}`；
- `.template.json` 只在正式数据文件缺失时复制，不覆盖已有用户数据；
- 部署 `plugin/sbm.dll`；
- 将两个 proxy loader 部署到游戏根；
- 删除旧 `plugin/eiem.dll`，防止双注入。

代码 DLL 在安装/更新时允许覆盖，并保留 `.bak`/`.pre_auto`；数据文件采用缺失时初始化。

## 9. 诊断与验证

主要输出：

- `<game root>/plugin/sbm_log.txt`
- `<game root>/plugin/breast_probe_log.txt`
- `SecondaryMotion/runtime/runtime_status.json`
- Manager 目录下的 `manager_startup.log`、`manager_changes.log`、`manager_crash.log`

当前自动验证覆盖 JSON、角色 DB/preset、gait whitelist、Jump 写门控、envelope、quaternion、hot reload、配置校验、Developer revision、bone dump JSON、动态 Runtime path 及 Manager wiring。

2026-08-23 本轮最终基线：109 tests passed、0 failed；Runtime build 和 Manager Debug build 均成功。该数字是当前工作树验证记录，不是永久发布版本号或产物 hash。

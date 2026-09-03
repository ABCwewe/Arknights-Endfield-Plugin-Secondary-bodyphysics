# EndfieldBreastMotion 启动响应、短点按与动作驱动升级可行性研究

日期：2026-08-23

状态：研究结论，不是实现；本轮未修改 Runtime、Manager、preset、角色 DB 或写入/Hook 主链

实验工作区：`D:\Project\EndfieldBreastMotion_test`
历史资料：`D:\Project\ShakerMOD_E_archive_20260819.tar.gz`

---

## 0. 结论摘要

本次三个问题并非同一个难度等级，建议严格拆开。

| 问题 | 可行性 | 当前最可信结论 | 推荐方向 |
|---|---:|---|---|
| 1. 冷启动体感慢 | 高 | `start` 早已被识别，不存在“先累计若干次才确认 gait”的机制；对健康且已捕获的 start，当前 preset 慢幅度包络是最强静态候选，但尚无现场端到端 timeline 证明它是用户实例的主要根因 | 先分层记录；若证实 captured-but-low，再单独增加“首次启动响应时间常数”，不改普通 gait 平滑、Hook、写入与 replay |
| 2. 连续短点按不出效果 | 高，但需先区分漏采与低幅 | archive 只证明若干约 200–250 ms 的 start→stop 片段被 20 Hz sampler 捕获；没有受控按键标签，短于 50 ms 的事件可能完全漏采。对已经捕获的 100 ms 运动，τ=1 s 时也只有 9.5% 幅度 | timeline 先分别统计 missed tap 与 captured-but-invisible；后者先试 fast onset/可选 impulse，前者需要另行验证便宜的运动边沿来源，不能靠包络修复 |
| 3. 跳跃、Idle、特殊动作与多轴 | 中高，但必须分阶段 | 固定正弦无法表达非周期动作；archive 的旧 clip 计时 jump 失败，不等于动作驱动不可行。archive dump 显示游戏 `MovementComponent` 具有 moveMode、fallingSpeed、velocity、acceleration 等更可靠信号，但当前游戏版本仍待 no-write probe | 保留已成功的 locomotion sine；新增模块化 Jump inertial source，再研究 Idle/body inertial source 和第二轴；不要一次替换整套算法 |

最重要的架构结论：

```text
已经稳定的部分保持原样：
PreLateTick / frame dedup / main-character identity / bone cache
/ qNative 读取时机 / 绝对 target 合成 / SetLocalRotation / replay

只在中间替换或增加“角度从哪里算出来”：
现有 Locomotion sine
+ 可选 Onset response
+ 可选 Tap impulse
+ 可选 Jump inertial
+ 可选 Body inertial / Native residual
→ 一次 Compose
→ 每骨每 unique frame 仍只做一次 primary absolute-target write
→ LateTick / SyncCalc 仍只幂等 replay 同一 target
```

所以第 3 个方向不是“推翻现有唯一可用写法”，而是把现有写入器保留为稳定执行层，在它前面增加新的 motion source。

---

## 1. 本次研究边界

### 1.1 绝对不碰的稳定区

以下部分是过去大量实验后得到的稳定链，本轮研究不建议改：

1. `AnimatorMono.PreLateTick` 的主 synthetic 写入时机；
2. Unity `Time.frameCount>=0` 时的全场景 frame dedup；解析失败返回 `-1` 的现有缺口只允许独立诊断；
3. 主角色识别、角色切换和骨骼重新解析；
4. 每帧先读取当前 native localRotation，再生成绝对 target；
5. 每个胸骨每 unique frame 只执行一次 primary `SetLocalRotation`，既有 LateTick/SyncCalc 只幂等 replay 同一绝对 target；
6. 已验证窗口内的 idempotent replay；
7. 正常 production motion 路径中 Jump disabled、unknown clip、unknown character 时的 fail-closed 语义；
8. immutable config snapshot、热加载和 last-known-good；
9. Runtime 热路径不得加入文件 I/O、JSON 解析、Sleep 或重复 Animator 全树扫描。

这些红线不把当前诊断工具误写成 production gate：AxisTester 被武装后会绕过 `pluginEnabled/motion_mode` 直接写骨骼，现有 TransformRecorder 被武装后会在 Hook 中 `fprintf`。所以任何“0 write/无文件 I/O”的测试前提都必须明确 AxisTester/recorder 未武装；新调查 recorder 则只能在热路径写内存 ring，Stop 后由 worker 写盘。

### 1.2 可以研究的计算区

允许在测试副本中逐项实验：

- gait classification 的附加语义标记，例如 `locomotionStart`；
- envelope 的首次启动时间常数；
- 不改变 angle 连续性的事件 impulse；
- JumpController 的状态和数学模型；
- 从 MovementComponent、root、Spine/parent 读取只读驱动信号；
- motion signal 从单标量扩展成 semantic 双通道，并映射为每侧一个 `dqR/dqL`；
- 最终 quaternion 的单次组合方式；
- test-only、allocation-free 的诊断 ring buffer。

### 1.3 可行性研究完成时的实际改动

可行性研究完成时只有本研究文档，没有实现实验，也没有更改主工作树。此后用户另行批准的 onset 最小实现记录见第 21 节；第 2.1 节 hash 是该实现前的研究基线，不是当前测试副本 hash。

---

## 2. 使用的证据与可信度

### 2.1 当前测试副本

重点检查：

- `src/motion/gait_classifier.h`
- `src/motion/gait_sampler.h`
- `src/motion/locomotion_envelope.h`
- `src/motion/synthetic_motion.h`
- `src/motion/jump_controller.h`
- `src/motion/motion_engine.h`
- `src/character/active_character.h`
- `src/character/character_identity.h`
- `src/il2cpp/animator_clip_reader.h`
- `SecondaryMotion/presets/Default.json`
- `verify_tests.cpp`
- `docs/GAIT_WHITELIST_UPGRADE.md`

本报告审计的是 onset 实现前的工作副本，不是干净 commit：

```text
HEAD = 043dacc275ba611ef6e487b2be99d8dd3930770e
Git porcelain entries = 43
status manifest SHA-256 = 3c7056ebb94880fc70afd969befd9876876347fadb29abf94da80d78172e1b36
本研究 MD = untracked
```

截至本次核对，主工作树与测试副本除本研究 MD 外文件内容一致；但两者共同包含此前尚未提交的 Runtime、Manager、数据和文档改动。因此这里的“当前源码”只代表该 dirty snapshot，不能由 HEAD 单独复现。关键输入文件 SHA-256：

```text
gait_classifier.h          26cae98989c3263ef736ff5002ba188c67b250d8b4ab35fab7a29cb83ef32b6d
gait_sampler.h             d7d08cef0393827ed2cc0ae97be0405a2bb1c38a9437d3c528ca1f82fc6fc3f3
locomotion_envelope.h      5a71a41861893f9de867a5f226ec4758fb6d52ae7325dc136a6192835e88946f
synthetic_motion.h         0f9d025b5f01eacad79b1aa08e8e13e5908aa07ae23e556eb5b94f84750caa85
motion_engine.h            8943988fb21a6fbc80c19b0a7abc4a47a79020c37763cb32c2816db17341f126
animator_clip_reader.h     cbf7dd0df9dfa6821b1b075d55903172432faa05c547c0b4875ac6d45b0519b2
active_character.h         656252feaba8980f3b739905b7a0e070163dd59fa439a67c979b12ed8367aa2f
Default.json               47638bd6777c3151f0d37fe4b363c89e4b64de4331d1432c17820cc5696c2e33
```

这些文件证据对该 snapshot 的静态行为可信度最高；用户现场实际部署版本仍需由 DLL/config hash 与 timeline 另行确认。

### 2.2 archive

archive 约 625 MB、2425 个文件。实际使用：

- `ShakerMOD/breast-probe/src/breast_probe.h`
- `ShakerMOD/plugin-parked-20260816/breast_probe_log.txt`
- `ShakerMOD/breast-probe/核心功能交接_20260816.md`
- `ShakerMOD/breast-probe/弹簧路线进展_20260816.md`
- `ShakerMOD/breast-probe/原生参数迁移调查_20260816.md`
- 三份 `breast_record*.csv`
- `ShakerMOD/il2cpp-dumper/IL2CPP_Dump_Normal/Gameplay.Beyond.dll.cs`

archive 可证明历史 clip、旧实验和当时 dump 的 API 结构，但不能直接证明当前游戏版本字段布局仍相同。因此：

- 类名、字段名和行为线索：中高可信；
- dump 中硬编码 offset/RVA：不可直接使用；
- 旧 CSV 的精确频率、阻尼拟合：低可信；
- 旧 Hook 覆盖故障：不能当作当前主链故障。

archive 早期曾出现“跑几秒乃至约十秒才可见、过两秒又回原生”的现象；同轮日志显示角度仍在计算和写入，历史结论是写回后又被动画/骨骼烘焙覆盖。加入 `CalcLayerMainStream` replay 后，用户确认效果能持续存在。因此旧的多秒级案例不能用来证明 gait 分类慢；当前版本已采用不同且已稳定的写入/重放链，本报告只把它作为“为什么禁止再改主链”的历史证据。

---

## 3. 现有启动链到底在做什么

## 3.1 `start` 已经生效

当前全局 locomotion classifier 对以下内容有明确处理：

```text
walk_start / move_start → Walk
run_start               → Run
sprint_start            → Sprint
zipline                  → Zipline（单独路径）
```

Generic `skill_start`、`battle_stop`、`interact_watch_start` 不会因为只含 `start/stop` 就误入 locomotion；它们还必须含 walk/run/sprint/move，或由角色 `animation_rules` 显式授权。当前回归测试也覆盖了 generic start/stop 排除。

archive 的 2026-08-16 旧源码已经有同样的 start 提前分类思路。因此“曾讨论过把 start 当作提前启动标志”这件事不只是讨论，它已经进入当前机制。

## 3.2 没有 gait 确认计数或 debounce

当前 sampler：

1. 每 50 ms 读取 Layer 0 clips；
2. 对当前 clip 直接分类；
3. 当次就设置 `currentGait`；
4. 没有要求连续命中 N 次；
5. 没有“先训练/暖机几秒”的 gait cache。

所以在 `ReadLayer0()` 成功的健康路径上，持续时间大于一个采样窗口的 `run_start` 通常增加 0–50 ms 采样相位，再叠加 Animator callback/Unity frame 调度抖动；50 ms 是 nominal interval，不是严格端到端上界。

如果一个动作短到完全落在两个 50 ms 采样点之间，理论上可能漏掉。但 archive 中实际 `run_start` 通常持续多个采样点，所以现在不应先把 gait sampling 从 20 Hz 提升到每帧。`GetCurrentAnimatorClipInfo`、数组和字符串读取是相对重的 IL2CPP 路径，贸然提高到 60 Hz 会增加热路径风险。

另有一个必须单独记录的失败语义：当前 reader 在 Animator 报告 clip count `<=0`、反射调用失败或返回空数组时返回 `false`；sampler 随即提前返回并保留上一份 `currentGait/transitionToIdle`。若 reader 成功但解析出的有效 sample 数为 0，则循环结束后反而会安装 GaitNone。两种“空”语义不同，不能统称为 `count=0`，timeline 必须记录 `readOk/reportedCount/parsedCount`。

当前也没有 stale-sample 最大年龄：若 `ReadLayer0()` 连续失败，上一份 gait/start/jump flags 可被无限保留。未来必须先用 timeline 量化正常 crossfade 的失败长度，再为 `sample_age_ms` 定义 bounded grace/expiry；不能无条件一失败就清 Idle，也不能继续永久 latch。

## 3.3 `run_start -> run_loop` 不会重新冷启动

`run_start` 和 `run_loop` 都映射为 `GaitRun`。切换到 loop 时：

- SyntheticRuntime 不重置；
- amplitude envelope 不重置；
- frequency envelope 不重置；
- phase 不重置；
- 不存在第二次 gait “确认”。

因此“start 判断一次，进入 run 又从零判断一次”不是当前代码的真实行为。

## 3.4 健康 captured-start 路径的首要候选是 amplitude envelope

测试副本的 `runtime/config.json` 指向 `Default` preset（该副本当前全局 `enabled=false`，本段只分析被选择 preset 的参数）。preset 有 19 个角色条目，其中 18 个为 enabled synthetic；`chr_0017_yvonne` 当前 disabled。18 个实际 synthetic profile 的参数分布：

```text
amplitude_attack_tau_sec:
  1.0 s × 15
  0.5 s × 3

frequency_tau_sec:
  0.2 / 0.5 / 1.0 / 1.5 s（按角色不同）

to_idle_release_tau_sec:
  0.2 s × 1 / 0.5 s × 7 / 0.6 s × 1 / 0.7 s × 6 / 1.0 s × 3
```

archive 最终可用版本的代表性包络曾是 attack 0.15 s、stop/to-idle release 0.015 s；当前 preset 明显更慢。这个对比说明，当前的亚秒到数秒体感不能直接套用旧版参数结论。

幅度包络是一阶响应：

```text
y(t) = 1 - exp(-t / tau)
```

从零启动的比例：

| 时间 | τ=1.0 s | τ=0.5 s | τ=0.15 s |
|---:|---:|---:|---:|
| 50 ms | 4.9% | 9.5% | 28.3% |
| 100 ms | 9.5% | 18.1% | 48.7% |
| 200 ms | 18.1% | 33.0% | 73.6% |
| 300 ms | 25.9% | 45.1% | 86.5% |
| 500 ms | 39.3% | 63.2% | 96.4% |
| 1000 ms | 63.2% | 86.5% | 99.9% |
| 到 90% | 2303 ms | 1151 ms | 345 ms |

这在量级上能解释“体感上可以感知的启动时间”：在 clip 已及时识别、包络从零开始的理想条件下，τ=1 s 时经过 100 ms，幅度 envelope 也只达到目标的 9.5%。它不能证明用户现场的 observation、classification、frequency envelope、write/replay 均健康，因此仍需 timeline 才能称为根因。

## 3.5 为什么第一次慢，紧接着再动较快，过一会又慢

对于“停一下马上再动较快，停久后再次变慢”，现有 envelope 本身就是一个会泄漏的短期记忆：

```text
冷启动：ampEnv ≈ 0                    → 慢
刚停止又启动：ampEnv 还没有衰减完      → 快
停更久：ampEnv 按 exp(-t/tau) 继续衰减 → 再次冷启动
```

停止衰减分两段：`run_stop/_to_idle` clip 存在时使用该角色的 `to_idle_release_tau_sec`；进入普通 GaitNone 后使用 `amplitude_attack_tau_sec` 向零。为说明后半段的“泄漏记忆”，若取 τ=1 s，其残留比例约为：

| GaitNone 持续时间 | 残留 |
|---:|---:|
| 0.2 s | 81.9% |
| 0.5 s | 60.7% |
| 1.0 s | 36.8% |
| 2.3 s | 10.0% |
| 4.6 s | 1.0% |

所以普通的“后续生效快，但停久后又需要冷启动”与当前数学高度吻合。但若具体现象是“已经松键，稍后反而出现一个明显峰值”，仅用这张残留表还不够；当前还有两个应优先记录的机制。

第一，当前 stop release 本身为 0.2–1.0 s，远慢于 archive 最终版本约 0.015 s。stop 已把 amplitude target 设为零以后，非零 envelope 与持续推进的 phase 仍可让瞬时角度先离开零交叉、再到局部峰值，尽管包络总能量正在下降。视觉上可能像“松开以后才启动一下”。

第二，若 `ReadLayer0()` 因 reported count `<=0`、异常或空数组而返回 false，sampler 会保留上一份 gait 和 transition flags。若上一份正好是 accepted start/Run，短暂读失败期间它仍会继续喂 locomotion target；直到之后成功读到 stop/GaitNone 才释放。这是当前源码存在的状态缝，但尚无 current-version timeline 证明它就是用户实例的根因。不能直接把所有 read failure 清为 Idle，因为正常 crossfade 也可能短暂无法读到 clip。

需要区分另一种描述：如果玩家一直按住、`run_loop` 持续十几秒，中途效果真的消失，现有包络数学不会自行做到这一点。那就必须寻找：

- clip 集合变成 unknown/GaitNone；
- clip read failure、parsed-empty 或状态恢复时序异常；
- eligible clip 的 raw weight 变成 non-finite，导致 weight-max 没有候选；
- stop/start blend 的状态聚合问题；
- 角色识别或骨骼重新解析；
- config/revision 切换；
- targetValid/write/replay 中断；
- 或当前版本之外的部署问题。

不能用“延长 gait 保持时间”掩盖这种持续跑动 dropout。

## 3.6 相位会增加少量、随机的体感差异

当插件全局 enabled、Synthetic `ComputeAngle()` 仍被调用时，phase 即使在 GaitIdle/GaitNone 的 no-primary-write 状态也会连续推进，不会在 start 重置。这能避免每次起步硬跳，但意味着开始写入时可能刚好接近零交叉。全局 `enabled=false` 时 `OnSyntheticCompute()` 会提前返回，不能把 SIGNAL_ONLY_NO_BONE_WRITE 的 phase 当作实际基线 phase。

当前 `Default.json` 18 个 enabled synthetic profile 的 gait-frequency 中位数及四分之一周期：

```text
Walk 1.86 Hz   ≈ 134 ms  （范围 1.7–3.0）
Run 3.0 Hz     ≈ 83 ms   （范围 2.24–3.1）
Sprint 3.72 Hz ≈ 67 ms   （范围 3.4–3.72）
Zipline 3.0 Hz ≈ 83 ms   （范围 3.0–4.0）
```

它能解释几十到一百毫秒的启动体感差异，但远小于 τ=1 s 带来的主延迟。第一轮不建议重置 phase，因为 phase reset 可能造成角度不连续和每次同方向硬抽动。

## 3.7 仍需验证的 stop/start overlap 假设

当前 GaitSampler 会对所有 clips 的 `transitionToIdle` 做 OR。理论上 blend 可能出现：

```text
旧 run_stop 仍在 clip 数组中
+ 新 run_start 已出现
→ gait 可以已经是 Run
→ 但 transitionToIdle 仍为 true
→ amplitude target 被强制为 0
```

archive 的旧 weight 读取经常为 0 或垃圾，不能用权重安全过滤旧 clip。因此这个假设有代码依据，也可能影响快速重按；但旧日志没有把同一次采样的完整 clip 数组可靠记录下来，当前不能把它写成已确认根因。

正确动作是先记录完整 timeline。如果证实，应使用明确事件优先级，而不是 weight：

```text
locomotion start 出现 → start 胜过残留 stop
只有 stop/loop、没有 start → stop 生效
unknown clip 永远不能取消 stop 或开启写入
```

## 3.8 当前 snapshot 还有两项不能当作稳定不变量

第一，角色切换的 reset 注释声称清空 envelope/jump，但当前 `SyntheticMotion::ComputeAngle()` 实际使用 `MotionEngine::synthetic_.envelope/jump`；`OnCharacterReset()` 只重建 `active_.synthetic/active_.jump/active_.replay` 和 amplifier，并没有重建或清零 `synthetic_`。`ResetActiveCharacter()` 也未显式清 `jumpDetected`。因此“角色/配置切换后内部 oscillator 一定归零”目前只是设计意图，不是源码事实；Phase A 必须记录角色切换、热更、死亡/重生后的 amp/freq/phase/jump state 是否继承。

第二，`GetUnityFrame()` 解析或 invoke 失败时返回 `-1`，当前逻辑仍会继续 compute/write，只是不再 dedup。timeline 必须记录 `unity_frame_valid`、每帧 callback ordinal 和 primary-write count；在正式实现任何新 source 前，先证明测试会话中 `unity_frame >= 0` 且每 unique frame 只有一次 primary write。

这两项是当前 dirty snapshot 的静态缺口，不是本轮修改主链的许可。先写 RED/诊断并确认现场后，才能把 reset 或 frame fallback 作为独立任务处理。

---

## 4. 问题 1：最小、低风险的启动优化

## 4.1 推荐方案：把首次 onset 和普通 gait smoothing 分开

不要全局降低现有 `amplitude_attack_tau_sec`。该参数也决定 Walk→Run、Run→Sprint 等平滑程度，用户已经调出满意的持续运动表现。

增加一个只在“无移动意图 → 合法 locomotion”上升沿生效的内部时间常数，例如概念上的：

```text
onset_attack_tau_sec = 0.08 ~ 0.15 s
```

状态规则：

```text
movingIntent = locomotion gait && !transitionToIdle

if movingIntent && !previousMovingIntent:
    start onset window

while onset window active and ampEnv < target:
    使用 onset tau
else:
    使用原 amplitude_attack_tau_sec
```

这里的上升沿不能只依赖 clip 名含 `start`，还要覆盖直接出现 run loop 的情况；也不能把 stop clip 当作仍在移动。

onset 生命周期必须明确：Walk→Run、Run→Sprint 等 gait upgrade 不重新开启；accepted stop/idle 或经验证的 movement-false 才结束 moving intent；read failure 既不签发新 onset，也不结束旧窗口。窗口还必须在达到目标、最大持续时间、角色/config epoch 变化、Jump/unknown/invalid 时强制失效。

## 4.2 为什么它不破坏现有效果

- 只改变冷启动前 0.2–0.4 秒；
- 到达目标后方程与当前版本相同；
- Walk/Run/Sprint 的持续幅度和频率不变；
- 普通 gait 切换仍用现有慢 smoothing；
- phase 第一轮不重置；
- 不增加 Hook；
- 不改变 frame dedup；
- 不改变 Compose/SetLocalRotation/replay；
- unknown、Jump disabled、invalid character 仍不写。

## 4.3 代表性离线估算

用 Run 35°/42°、固定 3 Hz、随机初相位，仅比较 accepted edge 之后的 onset 包络，得到以下代表性结果。它假设立即分类，不模拟 20 Hz 漏采、callback/frame 调度、Animator transition、`frequency_tau_sec` 或 write/replay；不是“按键到画面”的端到端实测，只用于判断 captured-start 的量级。

| 按下时间 | τ=1.0 s 峰值中位数 | τ=0.15 s 峰值中位数 | τ=0.08 s 峰值中位数 |
|---:|---:|---:|---:|
| 50 ms | 1.3° | 7.7° | 12.6° |
| 100 ms | 2.7° | 14.0° | 21.1° |
| 200 ms | 5.5° | 24.0° | 31.2° |
| 300 ms | 8.4° | 29.9° | 36.4° |

τ=1 s 时，100 ms 模拟中没有样本达到 5°；τ=0.15 s 时全部超过 5°。这说明对已经捕获 start edge 的问题 1/2，拆分 onset tau 很可能解决大部分体感；该离线表不包含 20 Hz 漏采，不能外推到 missed tap。

## 4.4 第一轮不建议做的事

1. 不把 gait sampler 全面改成 60 Hz；
2. 不把任何 unknown clip 暂时保持成 Run；
3. 不增加数秒 gait latch；
4. 不把 start phase 直接跳到正/负峰值；
5. 不为了解决冷启动而降低所有角色的原 attack tau；
6. 不引入 input hook、键盘 hook 或游戏输入模拟；
7. 不先读取 MovementComponent 才允许已有 locomotion 工作。

---

## 5. 问题 2：连续短点按

## 5.1 archive 证明部分短 start→stop 序列可见，但不是受控 tap 实验

旧 `breast_probe_log.txt` 有 11,304 条结构化 `[GAITDBG]`。其中 run/walk/sprint locomotion start 合计 184 条、9 个 clip 名、26 个连续段；细分：

```text
run_start   165 条
walk_start    4 条
sprint_start 15 条
jump_start   59 条
jump_land    68 条
run_stop    214 条
```

全部 locomotion stop 合计 358 条、9 个 clip 名、31 个连续段；表中的 214 只是 `run_stop` 子集。`run_start` 子集组成 21 个连续记录段。旧 sampler 每 50 ms 采样，`GAITDBG` 在 4 秒开/4 秒关的诊断窗口内每个样本记录数组首 clip。连续段约 1–18 个样本，即约 50–900 ms，中位约 250 ms。

后继不仅有 `run_loop`，也有 7 组可明确识别的 start 直接转 stop、没有 loop；其中多组 start 可观察窗口约 200–250 ms。这证明游戏确实存在短 start→stop 路径，但 archive 没有 keyboard down/up、tap 编号或视觉标签，不能断言这些片段全部来自玩家点按，也不能据此计算 tap 成功率。

局限：旧 logger 只写首 clip，不是完整 blend set；诊断窗口也会截断段落。因此这些数字用于证明 clip 序列存在和量级，不用于拟合精确动画持续时间，更不能视为 repeated-tap 视觉验收。

## 5.2 第一选择：先只做 fast onset

对于已经被 sampler 捕获的 100–300 ms 运动，fast onset 在这个区间能明显提高输出。最小实验应先回答：

```text
仅使用 onset tau 0.08/0.10/0.15 s，
是否已经能让连续点按有明显、愉悦的响应？
```

若答案是“够了”，不要再增加 impulse 模型。

但 fast onset 对“整段事件落在两个 50 ms sample 之间”完全无效；没有 start/moving edge，就没有任何东西可以触发 onset。Phase A 必须先按 tap 时长统计 capture rate，不能把 captured tap 的成功外推到 30–50 ms tap。

## 5.3 第二选择：start impulse，但必须连续

如果 fast onset 能快速出现运动，却仍表现为与点击节奏无关的连续正弦，可以再增加一个 event impulse：

```text
检测合法 locomotion start 上升沿
→ 给一个阻尼 oscillator 增加 velocity kick
→ impulse angle 与 locomotion angle 相加
→ clamp 后只 Compose 一次
```

不建议直接修改 angle 或把 phase 跳到峰值。速度 impulse 的优点：

- 当前 angle 连续，不会瞬移；
- 每次按键可增加一次可见能量；
- 快速连续点击可自然叠加，但能用上限约束；
- 不需要输入 Hook，直接使用已经观测到的 Animator/intent edge；
- 可以在当前 start/stop 被认可的写入窗口内衰减。

概念方程：

```text
on start edge: v += kick
v += (-2*zeta*omega*v - omega^2*x) * dt
x += v * dt
output += clamp(x, -tapMax, tapMax)
```

第一版不应让 impulse 在 unknown/普通 idle 中继续打开写入。先只在合法 start/stop locomotion 窗口内输出；若尾波被 idle 截断，再根据实测单独讨论一个很短、由已验证 start 授权的 tail ticket，而不是直接放宽全部 idle。

同样，Animator start edge 驱动的 impulse 只能改善 captured tap。若 no-write timeline 证明 30–50 ms tap 有显著漏采率，后续应独立比较三种事件来源：已解析且低成本的 `MovementComponent.isMoving/velocity` 边沿、经过性能基准的短时自适应高频 clip sampling、最后才是输入 Hook。任何一种都必须是可选 provider；不得让 MovementComponent 成为现有 locomotion 的启动前提，也不得先全局把 Animator 读取提高到每帧。

## 5.4 tap 触发条件

需要在 `GaitClassification` 中增加明确语义，而不是后续模块再次字符串搜索：

```text
locomotionStart
locomotionStop
locomotionLoopOrTransition
```

Tap 触发必须比 gait 映射更窄，定义为 `GroundLocomotionStart`：只接受全局已验证的 walk/run/sprint/move start 或经独立 provider 验证的地面 moving edge，并显式排除 `jump_start`、`jumpDetected`、Zipline、skill/special/dash。当前角色 `animation_rules` 只有“映射为哪个 gait”的语义，不能仅因某个自定义 `_start` 被映射为 Run 就自动获得 tap impulse；未来若确有需要，应增加独立 opt-in 语义并单独验收。

Repeated tap 的 edge 应基于：

```text
上一状态是 stop/idle/non-moving intent
当前状态是 accepted locomotion start/moving intent
```

目标只能表述为：每个“成功采样并接受的 GroundLocomotionStart edge”最多触发一次。start clip 持续 300 ms 只触发一次；只有 accepted stop/non-moving 以后观测到的新 start 才可再次触发。多次物理按键可能被 Animator 合并或漏采，不能承诺按键数与 impulse 数一一相等。

## 5.5 问题 2 的验收指标

除主观游戏观感外，测试副本应记录：

- start 首次出现时间；
- movingIntent 上升沿；
- 首次 envelope 达到 25%/50% 的时间；
- 首次 `abs(angle)` 超过 5° 或角色目标幅度 15% 的时间；
- 每次成功采样并接受的 GroundLocomotionStart edge 对应的局部峰值数量；
- 是否出现 angle discontinuity；
- stop 后多久 targetValid 失效；
- unknown、skill、battle clip 是否仍然零写入。

建议场景：

```text
冷 idle 5 s 后单次点按 × 20
30/50/100/150/250/500 ms 定时点按，各档多次覆盖不同采样相位
约 2 Hz 连续点按 10 s
约 4 Hz 连续点按 10 s
短按后立刻长按
长按后立刻连续短按
Walk / Run / Sprint 分别测试
```

每档除视觉外必须统计：输入/视频标记对应的 start capture rate、read failure/empty 率，以及最大输出是在松键前还是松键后。人工输入无法精确保持 30/50 ms 时，可用外部录屏/键盘时间戳作标注；第一轮仍不把输入 Hook 接入 Runtime。

不要先用输入自动化改变游戏；人工输入配合 timeline 足够判断第一版。

---

## 6. 问题 3：为什么需要新 motion source，而不是更多 sine 参数

当前模型本质是：

```text
angle = asymmetricAmplitude * sin(phase)
phase += 2*pi*frequency*dt
```

它非常适合：

- Walk；
- Run；
- Sprint；
- Zipline 等稳定近周期动作。

它不擅长：

- 单次 takeoff/apex/fall/landing；
- 非均匀时长的 transition；
- Idle 中一次抬手、转体、呼吸、弯腰；
- 左右转向或身体横摆；
- 落地冲击后频率和幅度都随时间衰减的尾波。

继续给每个特殊动作手调一个固定 frequency，只能匹配其中一个局部阶段，无法同时表达事件方向和冲击。

---

## 7. 候选高级路线比较

| 路线 | Jump | Idle/特殊动作 | 泛化 | 主要风险 | 结论 |
|---|---:|---:|---:|---|---|
| 每 clip 固定 sine 参数 | 低 | 中（仅周期 loop） | 低 | 仍无法表达非周期方向变化 | 仅作 fallback |
| clip normalizedTime + 手工曲线 | 中 | 高（已知固定 clip） | 低中 | transition 时 state time 未必等于选中 clip time；每角色维护多 | curated exception |
| 纯 clip start/land 计时状态机 | 中 | 低 | 中 | archive 已出现 segment 抖动和计时重置 | 不作为主方案 |
| root Transform 二阶差分 + spring | 高 | 低中 | 中高 | 噪声、teleport、坐标与 dt | Movement API 失败时 fallback |
| Spine/parent 旋转驱动 spring | 中 | 高 | 高 | 导数噪声、参数标定、LOD 更新 | Idle/general motion 主候选 |
| 游戏 MovementComponent + inertial spring | 高 | 中 | 高 | 需要稳定反射读取并确认字段语义 | Jump 首选 |
| 放大 native breast residual | 取决于官方 | 高（伊冯等） | 低中 | 多数角色 native 信号弱或不存在 | 可选混合 source |
| ML/神经网络在线推断 | 理论高 | 理论高 | 未知 | 数据不足、不可解释、热路径成本和回归难 | 当前不应采用 |

当前 `AnimatorClipReader` 已有 `ReadNormalizedTime()`，但反射找不到 `m_NormalizedTime` 时会 fallback 到固定 offset `0xC`。这个 fallback 可用于旧诊断，不足以作为高级 motion 的 fail-closed 输入：正式 source 只能在字段反射成功、值 finite、同一 state 内单调且重复动作对齐后标记 `normalizedTimeValid=true`。此外它是 Animator state time，不保证等于 BlendTree 内被选 clip 的独立 time；特殊动作也可能不在 Layer 0，必须先做 all-layer shadow inventory，不能直接把 Layer 0 结论推广到 Idle/Special。

推荐不是单选，而是混合但有明确优先级：

```text
Jump active       → Jump inertial 独占，不叠 locomotion sine
Locomotion active → 现有 sine + 可选 onset/tap impulse
Accepted Idle     → body inertial 或 native residual
Curated special   → 可选 clip curve
Unknown           → no write
```

---

## 8. archive 中的新突破口：MovementComponent

archive 的 `Gameplay.Beyond.dll.cs` 显示：

```text
Entity
  <movementComponent>k__BackingField
      ↓
MovementComponent
```

MovementComponent 暴露或保存：

```text
moveMode
isMovingOnGround
isInAir
isWalking
isInDash
logicPos / rootPos
acceleration
velocity
speed
isMoving
fallingSpeed
normalizedFallSpeed
desiredGait
actualGait
teleportedThisFrame
m_pipeline
```

MoveMode 在本次直接读取的 archive dump 中包含：

```text
None / Grounded / Falling / Jumping / AIJumping / PassiveJumping / Landing
Teleport / Flying / Charge / Simple / Spline / Programmed / External
Plunge / Blown / StepClimbing / Box / BoxFalling / Dash / Pivot / TurnStart
Follow / Animated / ManualMoveInSkill / AnimatedCamera / JumpingToTarget
GamePlayAbilityEntity / PhotoDragMove
Projectile / ProjectileStraightLine / ProjectileParabola
ProjectileBezier2D / ProjectileBezier3D / Bomb
```

archive 内另一份较早 dump 少了 `GamePlayAbilityEntity/PhotoDragMove`，已经说明历史版本之间枚举会变化。此前草稿中的 `Pivoting/Turning/Plunging/Climbing/Sliding/Swimming` 不是该 dump 的准确成员名，不应进入实现常量；当前游戏版本必须重新反射/探测。

`MovePipelineAir` 还包含：

```text
m_oldVelocity
m_originalVelocity
m_reachedApexThisIteration
inApexTime
landed
NotifyApex(...)
```

这说明游戏逻辑本身已经知道跳跃阶段。相比“看到 jump_start clip 后开始一个固定 0.27 s 计时器”，它更适合作为 jump gate 和事件来源。

### 8.1 如何低风险接入

不能使用 dump 里的固定 `0x258`、RVA 或旧版本地址。推荐：

1. 复用现有 `g_mainCharEntity`，并验证对象 class/identity；
2. 按字段名反射解析 Entity 的 `<movementComponent>k__BackingField`；
3. 对 MovementComponent 的每个候选按 dump 中真实形态分别处理：`moveMode` 走属性 getter，不假设存在同名字段；`fallingSpeed` 可探测 backing field；`velocity/actualGait` 等计算属性先验证 getter；
4. 元数据 method/field offset 可在初始化后缓存；角色切换/场景切换时重新取得并验证 MovementComponent 对象；
5. `m_pipeline` 会随 mode 更换，首轮不读其内部字段，更不能跨阶段缓存 pipeline 对象指针；
6. Runtime 热路径只进行已验证、allocation-free 的 bounded 只读；
7. 用 SEH、finite check、class/identity check 和耗时/失败计数；
8. 任一解析失败只关闭高级 source，现有 locomotion 继续工作；游戏更新后字段失效不能使 Runtime 全局 DISABLED_SAFE。

### 8.2 尚未证明的语义

archive dump 只能证明成员存在，不能证明：

- `acceleration` 是输入加速度还是真实最终加速度；
- `fallingSpeed` 的正负方向与单位；
- getter 在当前 PreLateTick 时点是当前帧还是上一逻辑帧；
- MovementComponent 当前版本字段名未变；
- root movement 是否受 elevator、slope、zipline、teleport 影响。

所以第一步必须是 no-write probe，不是直接拿这些值驱动胸骨。

---

## 9. 推荐的 Jump 模型

## 9.1 为什么 archive 的旧 jump 失败不能否定该方向

历史两段 jump 尝试依赖 clip segment flag 和局部计时：

```text
start segment
→ apex 前后固定曲线
→ land segment
```

archive 历史进展文档记录了 start/land segment 不稳定、局部 `t` 重置/倒退，并给出最终退回 landing-only damped sine 的用户测试结论；本次可取得的原始日志不足以独立重建每一次 flag 翻转。因此可确认的是“旧 clip-segment timing 路线未通过历史实测”，不能把每个具体翻转次数当成重新复算的事实。

该失败说明“用每个采样时刻的 clip level 直接重置 timer”不可靠；它没有证明 movement-state edge + kinematic forcing 不可行。

### 9.2 Jump 状态机

建议状态：

```text
Grounded / NativeOnly
  ↓ 精确 accepted Jump clip，或已验证的 player Jumping edge
Armed
  ↓ vy 向上超过阈值
Takeoff / Rising
  ↓ 速度穿零或已验证的 inApexTime
Apex
  ↓ 向下速度超过 hysteresis
Falling
  ↓ Landing mode 或 Air → Grounded edge
LandingImpact
  ↓ oscillator energy 衰减
Settled / Grounded
```

状态转换必须 edge-latched：进入一次只触发一次，不能因为 clip 每 50 ms 抖动而重置本段时钟。直接 `Grounded→Falling`（走下悬崖、平台消失、被击飞）第一版不视为 takeoff，不施加起跳 impulse；只有同一已武装 Jump epoch 才能从 Rising/Apex 继续 Falling/Landing。`PassiveJumping/AIJumping/Plunge/Blown/Dash/Teleport/Flying` 等模式默认 native-only，必须逐类验证和显式 opt-in。root 高度变化但没有 Jump 武装时（电梯、移动平台、斜坡、teleport）不得进入 Jump；Jump clip 存在但连续信号无效时，只允许已单独验证的 normalized-time curve，否则 native-only。

授权矩阵必须先于数学实现：

```text
已验证 player Jumping mode + identity valid
  → Movement provider 可独立签发 Jump epoch；此时 unknown Layer-0 clip 不授权 locomotion，也不否决 typed Jump source

Movement provider invalid + exact accepted Jump clip
  → 仅可进入单独验证的 curated curve / legacy landing fallback；否则 native-only

direct Falling without armed epoch
  → 第一版 native-only

Teleport / special-air mode / non-finite signal / epoch mismatch
  → invalidate，0 custom write
```

## 9.3 推荐数学：连续惯性 + 事件 impulse

每个视觉通道使用二阶系统：

```text
x'' + 2*zeta*omega*x' + omega^2*x = -gain * driverAcceleration
```

其中：

- `x` 是最终要映射到骨骼的角度状态；
- `driverAcceleration` 优先来自验证后的 vertical acceleration，失败时由 fallingSpeed/logicPos 求导；
- takeoff edge 可增加一次向下的 velocity impulse；
- apex 由连续动力学自然回弹，不强行跳相位；
- landing edge 根据落地前 `abs(fallingSpeed)` 增加一次向下冲击；
- 随后 spring 自然上下衰减。

它与目标观感的对应关系：

```text
身体起跳向上加速 → 胸部惯性落后，视觉向下
上升末端/顶点     → spring 回中并可能过冲到上方
下降加速           → 胸部相对向上
落地强制减速       → 胸部向下砸，再阻尼抖动归稳
```

如果 MovementComponent 的 acceleration 不包含重力，使用 `fallingSpeed` 的带滤波差分或 root velocity 差分补充，而不是假设字段语义。

## 9.4 Jump 的输出有效性

当前 `CanWriteSynthetic` 在 `jump.enabled=false` 时严格禁止 Jump 写入；开启后当前旧 JumpController 仍主要是 landing-only。新 Jump source 先返回 semantic 输出，再由统一 mapper 生成最终 MotionIntent：

```text
JumpSourceOutput {
  valid
  primaryAngle
  secondaryAngle
  sourceKind
  eventEpoch
}

PerSideMapper
  → MotionIntent { valid, characterEpoch, configRevision, dqR, dqL }
```

Jump active 时：

- Jump source 有效才写；
- 不应同时落回 Run sine；
- Jump source 无有效信号时 no write 或保留 native；
- 不能仅因为 `jump.enabled=true` 就让普通 Run sine 覆盖整个腾空期。

`LandingImpact→Settled` 跨入 Grounded/Idle 后仍需输出，必须由同一已验证 Landing event 签发 bounded tail ticket：携带 character/config/event epoch、最大时长、最大能量和 sourceKind；能量低于阈值、超时、角色/scene/config 切换、teleport、signal invalid 时立即失效。它是窄范围的 Jump 授权延续，不是普遍打开 Idle/Unknown 写权限。

Jump→Locomotion 或 Jump→Native 的 source handoff 还必须有角度连续性判据。第一版应在 mapper 的 rotation-vector 空间对 outgoing/incoming `dq` 做有界短过渡，或从 outgoing 状态初始化 incoming；不得一帧直接从 landing tail 跳到任意相位的 Run sine。验收同时检查角度 step、角速度和 ticket 失效，而不只检查 source priority。

这改变的是“算法输出是否有效”，不是 Hook、SetLocalRotation 或 replay 的机制。

## 9.5 数值安全

必须有：

- `dt` clamp；
- hitch 时固定小 substep，而不是一次大 Euler step；
- position/velocity/acceleration finite check；
- acceleration 和 jerk clamp；
- deadzone + low-pass；
- moveMode hysteresis；
- character switch、teleport、死亡、场景切换立即 reset；
- 最大 primary/secondary 角度；
- 最大 oscillator energy；
- 左右骨独立输出映射但共享事件时钟。

---

## 10. Idle 与特殊动作：更通用的 body-driven 路线

## 10.1 不应使用 Spine2 localPosition 作为唯一输入

archive 三份 recorder 中 Spine2 localPosition 基本不变，而 Spine2 localRotation 和胸骨 quaternion 有变化。旧 spring 方向曾尝试角加速度，但当时还同时受不稳定写入窗口、错误时间基准、低增益和 recorder 数据问题影响，不能据此断言 parent-driven spring 不可见。

更合适的 driver：

```text
root/MovementComponent 线性加速度
+
Spine 或胸骨 parent 的局部角速度/角加速度
```

胸骨本身不能作为唯一 driver，否则可能读取到上帧自己的写入并形成反馈。优先采样未被本插件修改的 parent/Spine。

## 10.2 Quaternion 驱动

不要直接对 Euler angle 做差。使用 quaternion log：

```text
qDelta = inverse(qParentPrev) * qParentNow
omegaBody ≈ log(qDelta) / dt
alphaBody ≈ (omegaBody - omegaPrev) / dt
```

记录/差分前先 normalize quaternion，并做 hemisphere continuity：若 `dot(qPrev,qNow)<0`，将 `qNow=-qNow`，再取 shortest-arc log；否则 `q/-q` 的等价表示会制造假大角速度。将 `alphaBody` 经过滤波、deadzone 和 clamp 后，驱动 vertical/lateral 两个 oscillator。

这能自动响应：

- Idle 转体；
- 抬手带来的上身摆动；
- 呼吸或较慢身体动作；
- 加速、减速、转向；
- 某些特殊动作。

但第一版只允许在明确认可的 Idle/relax 或角色专属 animation rule 中输出，不能对所有 unknown 动作普遍开写。

当前 `CanWriteSynthetic` 只允许 Walk–Zipline，`GaitIdle` 即使计算出非零 angle 也会被 fail-closed gate 拦截。因此未来不能只把 idle amplitude 从 0 调高；必须让计算层返回明确的 `sourceValid`，并让 gate 仅接受“Body/Native source 已验证且 clip 为 accepted Idle”的输出。现有 locomotion sine 不应因此自动覆盖全部 Idle，unknown 仍然无条件 no-write。

## 10.3 伊冯与 native residual

伊冯的官方胸部运动可作为参考，但存在两个不同用途：

1. 实时：在 native 信号确实存在的角色上使用/放大 native residual；
2. 离线：记录伊冯的 parent/root 输入与 native chest response，用于估计 spring 的 natural frequency、damping 和 gain 初值。

当前 `amplify_native` 在 LateTick 路径运行，而 synthetic 在 PreLateTick 计算/写入。不能为了做统一 mixer 就直接把 native amplification 搬到 PreLateTick；第一阶段应继续把二者作为互斥模式。概念架构中的 `NativeResidualSource` 只是更后期候选，只有单独重新验证读取/写入时序后才可混合。

不能直接使用 archive 的 `breast_record_yvonne.csv` 进行拟合。历史文档已经记录该文件曾被 Aurora 数据覆盖；同时旧 CSV 没有真实时间戳和 Unity frameCount，存在大量三连重复。

### 10.4 archive CSV 的数据质量问题

三份 CSV 都有 600 个 data row。排除第一列递增 callback `frame` 后，连续完全相同的运动记录段精确为：

```text
breast_record.csv:
rows 1–480   → 160 个三连段
rows 481–600 → 60 个二连段
合计 220 runs

breast_record_aurora_spring.csv:
600 rows → 200 个三连段

breast_record_yvonne.csv:
首段 2 rows
随后 99 个三连段（累计到 row 299）
rows 300–600 → 301-row 静态尾段
合计 101 runs
```

三份文件的 Spine2 localPosition 三列各自都只有一个唯一值，即完全不变；记录四元数的最大 `|norm-1|` 约 `7.7e-7`，数值本身没有明显损坏。但这些事实不恢复缺失的真实时间轴和 provenance。

旧 recorder 的 `frame` 是 callback 计数，不是 Unity frameCount；旧 Hook 又会被场景内多个 AnimatorMono 调用。旧分析脚本直接假设“60 行=1 秒”，因此其中 `1.66 Hz` 等精确频率不能当作新模型参数。

此外：

- 无 QPC/time_ms；
- 无 `Time.frameCount`；
- 无完整 clip set；
- 无动作开始/结束标签；
- 固定行号切段可能不是实际动作边界；
- Yvonne 文件 provenance 不可靠。

可保留的定性结论只有：parent rotation 是可观测信号，local position 很弱；不能保留精确拟合数值。

---

## 11. 多轴是否可行

数学上可行，而且不需要每轴分别写骨骼。

## 11.1 物理方向与骨骼 local axis 必须分开

“Z 是上下、Y 是左右”容易混淆三个空间：

1. Unity world space 通常 Y-up；
2. 模型/导出坐标可能 Z-up；
3. 每个胸骨的 local rotation axis 由 rig 决定，左右骨还可能镜像。

而且“绕某轴旋转”产生的是垂直于该轴的弧线，不等于“沿该轴平移”。因此配置和代码应使用视觉通道名称：

```text
primary / vertical sway
secondary / lateral sway
```

再为每个角色、每侧骨映射到 local X/Y/Z 和 sign，不能假设全角色 Z/Y 通用。

来自世界空间的 velocity/acceleration 必须先投影到当前角色 root 的正交 `up/right/forward` 基（并验证 root basis finite/normalized），得到 semantic vertical/lateral/forward；再分别经 `BR/BL` 映射到左右胸骨 local rotation-vector。不能把 world Y/Z 分量直接塞进骨骼 local axis。

## 11.2 一次 rotation-vector 合成、一次写入

首选概念：

```text
rotvecR = primaryBasisR * primaryAngle
        + secondaryBasisR * secondaryAngle
dqR     = QuatExp(rotvecR)
targetR = qNativeR * dqR

左骨使用独立的 primaryBasisL / secondaryBasisL
```

对于小角多轴输出，先在每侧局部 rotation-vector 空间求和、再一次 `Exp()`，比依次串联 Euler X/Y/Z 更容易定义 semantic channel，也避免无意的轴顺序差异。若原型暂时用两个 AxisAngle quaternion 相乘，乘法顺序必须固定并作为显式合同；正式设计仍应以每侧最终只产出一个 `dqR/dqL` 为边界。

分别 clamp primary/secondary 仍不足以限制组合结果：对 `|rotvecR/L|` 还要施加最终总角上限，`Exp()` 后检查 finite 并 normalize `dq`，与 qNative 合成后再 normalize target；任一失败令 intent invalid，不写骨骼。

然后仍然：

```text
SafeSetLocalRotation(R, targetR)  // 一次
SafeSetLocalRotation(L, targetL)  // 一次
```

绝不能先写 primary、再写 secondary；第二次会覆盖或叠加错误。最重要的兼容回归：

```text
secondaryAngle = 0
→ dq/target 必须与当前单轴路径数值等价
```

一般回归使用明确数值容差；若要求旧单轴路径位级不变，`secondaryAngle==0` 必须走现有 AxisAngle identity fast path，而不是强制经过新 Exp 实现。

## 11.3 左右骨映射与 common/differential

现有一个共享 `axis/sign` 对当前 primary 已经过人工校准。secondary 未必能共享同一 sign，左右骨的局部 basis 也可能镜像。推荐先在语义空间形成：

```text
common       = 共同上下/前后惯性
differential = 身体 roll/yaw 造成的一上一下或左右差动
semanticR    = common + differential
semanticL    = common - differential
localR       = BR * semanticR
localL       = BL * semanticL
```

其中 `BR/BL` 是每侧独立的语义轴→局部 rotation-vector 映射。最简单的离散配置也可能要求：

```text
right.secondary.sign = +1
left.secondary.sign  = -1
```

第一轮只在 Developer/小号做夸张 ±10° Axis Test，不持久化到正式 DB；方向确认后才讨论 schema。每侧还应有独立 gain、每轴 clamp 与总角度 clamp。

## 11.4 不建议第一步做完整 3D

先做两个独立标量 oscillator：

```text
vertical channel
lateral channel
```

已经足以验证价值。完整 3D mass-spring、碰撞、左右耦合会显著扩大调参和稳定性问题，不适合作为首个高级升级。

---

## 12. 如果仍采用“手动匹配”，怎样更简单

手调每个动作的单一 amplitude/frequency 只适合规则 loop。可以分两类改进。

## 12.1 周期动作：离线自动建议参数

Developer recorder 记录 parent/root signal 后，离线工具可以：

1. 去趋势；
2. autocorrelation/FFT 找主频；
3. 从局部峰值估计 amplitude；
4. 估计左右/上下 phase；
5. 输出建议的 frequency、gain、damping；
6. 人在游戏里只做最终美术确认。

这比在 Runtime 热路径做频谱估计更安全，也避免每次启动要等 1–2 秒观察窗口。

如果一个 idle loop 明显不是单频，可用两个 harmonic：

```text
A1*sin(2*pi*f*t + phi1)
+ A2*sin(4*pi*f*t + phi2)
```

但应当 phase-lock 到经过验证的 Animator normalizedTime，否则长期会和动作漂移。

## 12.2 非周期动作：事件曲线模板

可以使用标准模板：

```text
jump_standard
land_heavy
land_light
idle_turn
idle_reach
```

每条曲线包含少量 primary/secondary keyframe，并由事件或阶段归一化时间驱动。Jump 不建议按固定总时长归一化，应以 movement state、apex、landing event 分段。

角色 DB 的 animation rule 未来可以选择一个 source/profile，而不只是映射 gait。该 schema 现在不应实施，先证明一个 Jump source。

## 12.3 更通用的自动拟合

若采到可靠 reference，可离线拟合二阶离散状态空间/ARX：

```text
input:  root acceleration + parent angular acceleration
output: native chest residual
```

在 held-out 动作验证后，得到 natural frequency、damping、gain 的初值。它比神经网络简单、可解释、可在 C++ 热路径中用少量标量运行。

---

## 13. 推荐的新内部架构

概念接口，不是本轮代码：

```text
MotionSignals
  gait / start / stop / transition
  clip set / normalizedTime + validity provenance
  moveMode / actualGait
  velocity / acceleration / fallingSpeed
  root pose / parent pose
  characterEpoch / configRevision / frame

        ↓

LocomotionSource        // 保留现有 sine
OnsetResponseController // 只调首次 envelope
TapImpulseSource        // 可选 additive
JumpInertialSource      // Jump 时独占
BodyInertialSource      // accepted Idle/special
NativeResidualSource    // 更后期、时序另证

        ↓

MotionMixer + PerSideMapper
  source priority / common-differential / clamp / rotvec→dq

        ↓

MotionIntent
  valid
  characterEpoch
  configRevision
  sourceKind
  dqR
  dqL

        ↓ 写链隔离边界

现有单一计算点：
  qNativeR/L 各读一次
  targetR/L = qNativeR/L * dqR/L
  缓存绝对 target
  primary SafeSetLocalRotation 每骨一次
  LateTick / SyncCalc 只 replay 同一绝对 target
```

Mixer 规则必须简单、可测试：

```text
Jump > Locomotion > Accepted Idle > Unknown
Tap impulse 只加在 Locomotion
Unknown 永远 invalid
characterEpoch/configRevision 不匹配永远 invalid
```

不建议让多个大 source 无限制相加。

---

## 14. 先做诊断，不先改算法

## 14.1 新 timeline 需要记录什么

一次 unique Unity frame 一条运动/输出记录，至少包含：

```text
time_ms
unity_frame
unity_frame_valid
character_id
animator pointer / entity pointer
callerAnimator / callback ordinal / first-callback-is-main
gait sample readOk / failureReason
sample_seq / fresh / sample_age_ms
reported clip count / parsed clip count / capacity / truncated
每个 clip array index + name（固定容量）
每个 clip raw weight / isfinite
每个 clip 的分类结果
selected gait / selected reason
locomotionStart / locomotionStop
transitionToIdle / jumpDetected / landingDetected
movingIntent
state hash / normalizedTime / reflectionResolved / fallbackUsed
外部 tap/video marker（若有，只作对齐，不进入控制）
MovementComponent resolve status
moveMode / actualGait / isMoving / isMovingOnGround / isInAir
velocity xyz / acceleration xyz / fallingSpeed
root position/rotation
parent local quaternion
native breast R/L quaternion
envelope amp/freq/phase
sourceKind
primary/secondary angle
targetValid
本帧 primary-write/replay/axis-test 次数 + finalized
```

clip set 仍只能沿用现有 20 Hz `GaitSampler` 读取：在真正的 gait sample 时更新固定容量缓存，并给缓存增加 `sample_seq/fresh/sample_age_ms`；每帧记录只复制最近缓存，绝不能为 timeline 额外调用一次 `GetCurrentAnimatorClipInfo`。原始数组超过固定容量时必须记录 `truncated=true`，不能把未读取尾部当作不存在。MovementComponent 字段在 Phase D provider probe 之前保持 absent；之后 getter 也只先做低频成本/语义验证，连续物理量才考虑已解析字段的 allocation-free 只读路径。

`ObserveCallback` 在 frame-dedup write gate 之前对每个 callback 运行，而 compute/write 只在该 Unity frame 的第一个 callback 运行；主角色 callback 可能晚于首 callback。因此 timeline 应分别记录 `GAIT_SAMPLE` 事件（真正读取主 Animator 时）与 `FRAME_OUTPUT` 记录（实际计算/写入时），再用 `sample_seq/sample_age_ms/unity_frame` 关联，不能假设同一帧一定先采 gait 再写。

`native breast R/L quaternion` 必须在本插件 Compose/SetLocalRotation 之前采样或直接复用 Compose 已读取的 qNative；当前 diagnostics recorder 位于 `OnSyntheticCompute()` 之后，在 Synthetic 模式下可能已经读到 custom target，不能把该值标为 native。记录行应同时区分 `qNativePreWrite` 与 `qTarget`，且不能为诊断多做一轮写入。

所有记录 quaternion 先 normalize，并相对上一样本做 hemisphere continuity；差分和可视化使用 shortest arc，避免 `q/-q` 符号翻转形成假峰值。

采集模式必须分开标记：

1. `SIGNAL_ONLY_NO_BONE_WRITE`：全局 `enabled=false`，并确认 AxisTester/所有诊断写入未武装。当前 MotionEngine 仍会执行角色识别和 gait `ObserveCallback`，而正常 Synthetic compute、ReplayTarget、LateTick 写入都会被 `pluginEnabled` gate 拦截；适合验证现有 clip/read/root/parent/native 信号，但此模式不会推进现有 envelope。AxisTester 位于 production write gate 之外，若被武装仍会直接 `SetLocalRotation`，所以仅凭 global disabled 不能证明零骨骼写入。
2. `BASELINE_OBSERVE`：只在小号启用当前原样 locomotion，新增 ring 仅旁观现有 envelope、qNativePreWrite、qTarget、targetValid/write/replay；不增加任何新 motion source。只有这个模式能验证当前实际输出链和持续 dropout。

若需要在 no-write 数据上研究 envelope，应把记录的 gait/events 离线喂给纯 C++ SyntheticMotion 副本；不能在运行时悄悄推进正式 `active_.synthetic` 再声称“没有影响状态”。

## 14.2 不能照搬 archive 或当前 recorder

archive 旧 recorder 在 Hook 中直接 `fprintf`，并且没有 frame dedup，因而产生多实例 callback 重复。当前 `TransformRecorder` 已位于 MotionEngine 的 frame dedup 之后，但 `Tick()` 仍在 PreLateTick 热路径直接 `fprintf`；worker 只负责触发 `Start()`，并没有把逐帧写盘移出热路径。新 timeline 字段远多于现有 recorder，不能在该实现上继续堆叠。

新的 test-only recorder 应：

1. 热路径写固定容量 ring buffer；
2. 不分配字符串；clip 名拷入固定 char 数组；
3. Unity main hook 是唯一 producer；每帧只写一个 slot；
4. 本帧 replay/axis 次数要到后续 callback 才完整，因此下一 unique frame 到来时 finalize 上一 slot，Stop 时显式 seal 最后一 slot；
5. worker 只能 atomic 请求 Stop，main hook seal 后以 release 发布；worker acquire 看到 SEALED 后才 flush，捕获期间不并发读取 ring；
6. buffer 满则 seal/停止并记录 overflow，不阻塞游戏；scene unload/reset 只写终止原因；异常进程退出允许丢失未 flush 的调查数据，不能在 Hook 中抢救写盘；
7. recorder 输出诊断文件，所以准确称呼是 no-bone-write recorder，不是“无 I/O”；正式包默认不启用，最好仍排除调查工具。

## 14.3 场景矩阵

先只采集，不写新 motion。Phase A 先执行不含 MovementComponent 的现有链场景；表中 Jump/movement 连续量列在 Phase D provider probe 后再执行：

| 场景 | 目的 |
|---|---|
| idle 5 s → Run 长按 15 s | 冷启动和持续 dropout |
| Run stop 0.2/0.5/1/2/5 s → 再 Run | 验证 residual envelope 解释 |
| 30/50/100/150/250/500 ms tap + 连续点按 2 Hz / 4 Hz | capture rate、read failure、start/stop overlap、松键前后峰值 |
| Walk/Run/Sprint 各 10 次起步 | gait 差异 |
| idle jump ×10 | moveMode/fallingSpeed/apex/landing |
| run jump / sprint jump 各 ×10 | 不同 jump path |
| 轻落地/重落地 | impact signal 是否区分 |
| 伊冯 idle/特殊动作 | native reference 与 parent driver |
| 角色切换后立即移动 | 500 ms identity refresh 是否参与一次性延迟 |

---

## 15. 严格分阶段实施建议

下面每一阶段都应独立批准、RED、最小修改、build、游戏验收；不能一次合并。

### Phase A：双模式只读 timeline

目标：只用现有 clip/gait/envelope/target/write 链确认用户报告到底是 sampler 漏采、read failure 保留旧状态、captured-but-low envelope、慢 release 延迟峰值，还是持续跑动 dropout。

A1 `SIGNAL_ONLY_NO_BONE_WRITE` 可先在全局 disabled、AxisTester/诊断写入未武装下运行：

- gait sample 事件必须区分 read failure、reported count 与 parsed count；
- 30/50/100/150/250/500 ms 各档得到 start capture rate；
- native/root/parent 信号无插件输出污染；
- 全程 Compose/Set/replay 计数为 0。
- `unity_frame_valid` 始终为 true；角色切换/热更/死亡重生前后能看见 synthetic state 是否继承。

A2 `BASELINE_OBSERVE` 只在小号启用当前原样效果：

- `GAIT_SAMPLE` 与 `FRAME_OUTPUT` 用 seq/age/frame 正确关联，不强求同一行同一 callback；
- 连续跑 15 s 时能定位任何 gait/targetValid/write/replay gap；
- captured tap 能量化 gait accepted → `|angle| > 1°/3°/5°`；
- 能区分 stop 后 delayed peak 与新一次 start；
- recorder 停用时热路径无额外文件 I/O。

Phase A 先增加不依赖游戏的 reader/classifier 回归：multi-clip 数组顺序置换、全零权重、负权重、NaN/Inf 权重、超过固定容量、reported>parsed、一次/连续 read failure，以及 start+stop+loop 同帧组合。结果必须与数组顺序无关；`truncated`/non-finite/failure 不能静默变成普通 Idle。

还要覆盖生产 gate 笛卡尔积：global enabled/disabled × motion mode off/native/synthetic/amplify × profile enabled/disabled × gait unknown/idle/locomotion/jump/zipline，再分别令 AxisTester/旧 recorder armed/unarmed，证明“production 0 write”与“诊断例外”没有混淆。

Phase A 的退出必须分叉：

```text
start 已捕获，但 angle/envelope 太低 → Phase B fast onset
start 经常完全漏采                → 先做可选 event-provider spike
持续 run 中 write/target dropout  → 单独诊断，不进入 onset
```

不得把三类结果合并成一个“启动优化”。

### Phase B：onset tau 纯数学与离线 replay

先写 RED：

```text
当前 τ=1 s 的 captured 100 ms tap 达不到设定可见阈值
没有 accepted edge 的 tap 不得被 onset 凭空触发
start→run 不应重置 envelope
unknown/jump disabled 不得 valid
secondary=0 必须保持旧输出
direct loop without start 可开启一次 onset；Walk→Run→Sprint 不重复开启
Zipline/jump/custom-rule `_start` 不得触发 GroundLocomotion TapImpulse
character/config/scene reset 与长 dt 后 onset/impulse state 清空
```

最小实验：只在 test 中加入 onset window，先用夸张的 0.05 s 验证生效，再比较 0.08/0.10/0.15 s。

游戏只测 Walk/Run/Sprint：

- 冷启动；
- 2 Hz/4 Hz 点按；
- 连续 15 s；
- stop；
- unknown/special；
- 角色切换。

若已经满足问题 1/2，停止，不做 impulse。

### Phase C：可选 TapImpulse

只有 Phase A 已证明 start 被捕获、而 Phase B 仍无法产生“一按一反应”才进入。若 start 根本漏采，TapImpulse 使用同一个缺失 edge，也不会解决问题。

RED：

- 一个持续 300 ms 的 start 只能触发一次；
- 两个都被 sampler 成功接受的 start→stop→start 触发两次；
- 连续 impulse 不产生 angle discontinuity；
- energy 和 angle 有界；
- idle/unknown 不打开写入。

### Phase D：MovementComponent no-write probe

这是独立 provider spike，不和 Phase A recorder 一次实现。若 Phase A 证明 missed tap 是主要分支，可在 Phase B/C 前提前执行 D；否则按原顺序先完成 captured-start 的低风险 onset 判断。

仅解析和记录：

- field/method resolution；
- moveMode enum；
- fallingSpeed；
- acceleration；
- actualGait；
- teleport marker。

除 Jump 阶段外，同时对照 `isMoving/velocity` 是否能提供比 20 Hz Animator 更及时、且无战斗/技能误触的 locomotion edge；它只作为 missed-tap provider 候选，不替代现有 gait whitelist。

解析失败只写诊断状态，不影响当前功能。

### Phase E：Jump 纯 C++ source + 新 timeline replay

先不接骨骼写入。archive CSV 没有 Movement/真实时间信号，只能提供定性历史背景；定量 replay 必须使用 Phase D 新 timeline。纯数学测试注入确定性 `dt`，不得依赖 QPC/Sleep。给新记录逐帧输入 Jump source，输出 CSV/图：

- takeoff 必须先向下；
- apex 附近回中/向上；
- falling 保持合理方向；
- landing 冲击与落地速度相关；
- 0.5–1.5 s 内阻尼归稳；
- frame hitch/teleport 无爆炸。

### Phase F：Jump 小号游戏实验

只在测试副本、单角色、夸张参数；旧 locomotion 路径不动。通过后才添加第二角色验证通用性。

### Phase G：第二轴

先做 no-write quaternion replay 和 Axis Test，再在 Jump landing 中只加入很小 lateral response。确保 `secondary=0` 与旧路径等价。

### Phase H：Idle/body inertial

最后进行，因为它的动作范围最广、误触写入风险最高。当前没有独立 Accepted-Idle 授权 schema；先增加明确 allowlist/source-profile 入口并做 round-trip 回归，再限定伊冯和一个无 native motion 的角色、少数明确 idle clips。不能直接把普通 Idle 或 unknown 视为授权。

---

## 16. 回归与性能红线

任何实验进入生产前必须保持：

```text
未知角色：0 write
unknown clip：0 write
Jump disabled：0 write
motion_mode off：0 write
角色切换：旧 target invalid
unity_frame 无效：高级 source 不可 valid；不得退化为多次 primary write
每骨每 valid unique frame：最多 1 次 primary write
replay：仍是同一绝对 target
无 quaternion NaN/Inf
无新文件 I/O/JSON/lock/managed allocation 热路径
无 Animator 全树重复扫描
现有 Walk/Run/Sprint/Zipline 稳态幅度和频率不变
```

上表只描述正常 production motion path，测试时必须保证 AxisTester/旧 recorder 未武装。`unknown clip` 的精确定义是“unknown clip 本身永不授权写入”：未来若经验证的 player `Jumping` typed provider 独立签发 Jump epoch，Layer-0 clip 可为 unknown，但写权限来自 typed Jump source；provider 无效时仍为 0 custom write。`Jump disabled` 还必须立即取消任何 Jump tail ticket。

性能计数建议：

- IL2CPP invoke 次数/frame；
- metadata resolve 次数/session；
- timeline ring 写入耗时；
- MotionSource compute 微秒；
- frame hitch reset 次数；
- dropped diagnostic samples。

---

## 17. 风险清单与回退

| 风险 | 处理 |
|---|---|
| 把冷启动误当持续 dropout | Phase A 同时记录 clip、envelope、targetValid、write |
| 把 missed tap 当成 envelope 太慢 | 按 30–500 ms 统计 capture rate；无 accepted edge 时不得进入 onset/impulse 结论 |
| reader failure 保留旧 gait | 分开记录 readOk/reported/parsed；先证明时序，不把所有失败粗暴清 Idle |
| stale gait 无期限 latch | 记录 sample_age/failure streak；先量化合法 transition grace，再设 bounded expiry |
| stop/start overlap 推迟 onset | 先取完整 clip set；证实后用 start edge precedence，不用 weight |
| fast onset 使起步过猛 | onset 独立参数；先 0.05 夸张验证，再回调 0.08–0.15；不重置 phase |
| tap impulse 叠加爆炸 | velocity/energy/angle clamp；只在合法 locomotion |
| MovementComponent 游戏更新失效 | 按名称反射、可选 provider、失败回落现有 gait，不用固定 offset |
| normalizedTime 固定 0xC fallback 误读 | 高级 source 仅接受反射成功且 shadow 验证通过的 state time；fallback 只作旧诊断 |
| Special 不在 Layer 0 | 先做 all-layer shadow inventory；未确认 owner/layer 时 native-only |
| acceleration 语义不符 | no-write timeline 对照 fallingSpeed/root finite difference |
| 导数噪声 | quaternion log、low-pass、deadzone、jerk clamp、dt substep |
| teleport/切角色情况爆炸 | teleported flag/位移阈值/角色 reset 立即清 oscillator |
| 当前内部 `synthetic_` 未随角色 reset | Phase A 先记录状态继承；确认 RED 后作为独立修复，不能顺手混入 onset |
| `Time.frameCount=-1` 时 dedup 失效 | 记录 frame validity/callback ordinal；高级 source fail closed，独立修复 fallback |
| AxisTester/旧 recorder 破坏“0 write/无 I/O”假设 | 采集前显式检查未武装；timeline 记录 axis-test count；新 recorder 只写 ring |
| 多轴顺序或镜像错误 | rotvec 求和后一次 Exp；secondary=0 等价测试；左右独立 Axis Test；一次 Compose |
| Idle 对战斗动作误写 | 只对 accepted Idle/角色 rule，unknown fail closed |
| 伊冯 native + procedural 双重过量 | 角色选择 native residual 或 body source，默认不同时全增益叠加 |
| 新功能破坏社区已满意 locomotion | 新 source 默认关闭；现有 sine 保留且优先做局部增量 |

所有实验应能通过一个 feature flag 或回退到旧 source；不以“修复失败后再恢复 Hook”作为回退方式，因为 Hook 从一开始就不应改。

---

## 18. 最终建议顺序

### 第一优先：问题 1 + 问题 2 共用的最小实验

```text
完整 timeline
→ captured-but-low：onset-specific tau → 游戏验证 → 必要时 TapImpulse
→ missed tap：可选 movement-edge provider spike，不先改包络
→ sustained dropout：单独调试，不进入上述两条
```

对已经捕获的 tap，fast onset 是低风险、高回报项；对漏采 tap，现有 archive 没有足够数据预测 provider 成功率。

### 第二优先：Jump 作为高级算法突破口

Jump 比 Idle 更适合作为第一个新 source，因为：

- 所有角色的物理阶段相似；
- archive `MovementComponent` 提供明确状态和连续量的候选入口；
- 目标观感清楚；
- 可以在 `jump.enabled` 下完全 opt-in；
- 当前 Jump 默认关闭，不会影响现有用户；
- 成功后得到的 inertial oscillator、信号 provider、MotionIntent、多轴基础都能复用于 Idle。

### 第三优先：第二轴和 Idle/body inertial

Jump 单轴稳定后再加第二轴；随后用同一 driver/oscillator 基础研究 Idle。不要先做“所有动作通用 3D 物理”，也不要一次暴露大量 Manager 参数。

---

## 19. 对三个原始问题的直接回答

### 1. 是否有办法尽可能减少启动时间而不碰写入机制？

有，而且对健康、已捕获的 start 可行性高。`start` 已经识别；关键是它目前仍然走 0.5–1.0 s 的慢 amplitude attack。把首次合法 locomotion onset 单独使用 0.08–0.15 s 的时间常数，能只改变起步，不改变稳定动画和写入主链。与此同时，reader failure 保留旧 gait、0.2–1.0 s stop release 与 phase 造成的松键后峰值必须先由 timeline 区分；先不要盲改采样率、phase 或 gait hold。

### 2. 能否支持不断短点按触发乳摇？

能，但分两种。archive 只证明若干约 200–250 ms 的 start→stop 片段曾被捕获，没有受控 repeated-tap 输入数据。对 captured tap，fast onset 是第一项低风险候选，是否足够仍由 timeline/游戏阈值决定；若仍不足，才给每个成功接受的 GroundLocomotion edge 一个有界 velocity kick。对完整落在两个 50 ms sample 之间的 missed tap，这两种方案都不会触发，必须先验证 `MovementComponent` movement edge、受控的自适应采样或最后才考虑输入 Hook，不能声称仅靠包络必然解决。

### 3. 能否跳出固定间隔并引入更贴近动作的多方向运动？

能，但应采用混合升级而不是替换成功的 locomotion：

```text
Locomotion 保留 sine
Jump 使用 MovementComponent + 惯性 oscillator
Idle/特殊动作使用 parent/root 驱动 oscillator
有官方信号的角色可选 native residual
特殊固定 clip 才使用手工/自动拟合曲线
最后扩展第二输出轴
```

Jump 是最适合的突破口。archive dump 已经证明旧版本类型中存在 moveMode、fallingSpeed、velocity、acceleration、apex/landing 等候选成员；真正的不确定性不是理论可行性，而是当前游戏版本能否稳定、低成本地解析，以及字段语义与时点是否正确。这个不确定性可以用 no-write probe 验证。

---

## 20. 下一项建议

若批准继续，下一项只做：

```text
在 EndfieldBreastMotion_test 中新增 test-only、frame-dedup 的 motion timeline recorder
不改变任何角度计算
不写新 motion
不解析 MovementComponent
AxisTester/现有 recorder 未武装
不改主工作树
```

先用它回答三个问题：

1. 冷启动延迟中 classifier/read failure 与 envelope 各占多少；
2. repeated tap 的 start capture rate、read failure 与完整 start/stop clip set 如何 overlap；
3. 连续按住 15 s 是否真的出现 gait/target/write dropout；

拿到这三项证据后按 Phase A 分叉：captured-but-low 才单独进行 onset tau 的 RED→最小修改→游戏验证；missed tap 则先独立批准 Phase D MovementComponent provider probe；持续 dropout 单独诊断。不能把三个实现合并。

---

## 21. 2026-08-23：问题 1 onset 最小实现记录

用户在研究完成后批准只实现问题 1，不加入 Manager/UI/JSON/preset 参数。测试副本新增 Runtime 内部常量：

```text
kLocomotionOnsetAttackTauSec = 0.10 s
```

行为合同：

- 仅 `Walk/Run/Sprint` 从非地面移动进入 accepted ground locomotion 时启用；direct loop 同样可触发；
- Zipline、transition-to-idle、Jump 不启用；
- 两侧 envelope 都进入目标 5%（至少 0.001 rad）后退出；
- Walk→Run→Sprint 等移动中 gait 切换继续使用原 `amplitude_attack_tau_sec`；
- stop/idle、角色切换和 config refresh 会清 onset epoch；
- 不重置 phase，不改变 frequency/release、Compose、write、replay 或 Hook。

修改范围：

```text
src/character/active_character.h
src/motion/gait_classifier.h
src/motion/locomotion_envelope.h
src/motion/synthetic_motion.h
verify_tests.cpp
```

验证结果：onset RED→GREEN 回归加入后 `118 passed, 0 failed`；UI serialization、JSON、forbidden-hardcode scan 和测试副本 Runtime build 通过。`bin/sbm.dll` SHA-256：

```text
19e7da3389d2458ae37281a8b411697abdce20834978f345c573064ac5c9451a
```

当前日志不足以客观比较这次 100–300 ms 级变化：`[GAIT]/[CLIP]` 与 `[PRE] gait/angle` 都约 2 s rate-limit，且没有 onsetActive、selected attack τ、envelope 或 edge timestamp。单元测试能证明代码选择了 0.10 s，但游戏内输入→视觉效果仍需肉眼测试；若肉眼无法判断，下一项应单独做 event/timeline 诊断，而不是用现有日志下结论。


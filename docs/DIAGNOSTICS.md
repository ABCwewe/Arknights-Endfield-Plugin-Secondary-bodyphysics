# 诊断与 Developer Mode

更新日期：2026-08-23

## 1. 正式控制通道

当前正式 Developer 流程不要求用户新建 txt 文件：

```text
Manager
→ SecondaryMotion/runtime/developer_command.json
→ Runtime worker 低频解析
→ 主线程 hook 执行需要 Unity API 的工作
→ bone dump / CSV / log / runtime_status
```

Manager 每次写 command 都增加独立 Developer revision。Manager 重启时读取现有 `developer_command.json.revision` 后继续递增；Runtime 只接受严格大于最后处理值的 revision。

command expiry 或 clear 只清 active state，不清 last processed revision，因此磁盘上的旧 axis-test command 不会在下一轮重新激活。

该 revision 与 `runtime/config.json.revision` 是两套独立通道：

- config revision：preset/DB hot apply；
- Developer revision：Scan、Axis Test、Record、Clip Inspect。

## 2. developer_command.json

### one-shot

```json
{
  "revision": 42,
  "command": "bone_scan"
}
```

支持的 one-shot 包括 `bone_scan`、`clip_inspect`。Runtime 消费后不会把相同 revision 再执行一次。

### axis_test

```json
{
  "revision": 43,
  "command": "axis_test",
  "axis": "Z",
  "sign": 1,
  "angle_deg": 5,
  "expires_ms": 5000
}
```

Axis Test 是可选诊断，不是新角色入库必经步骤，其选择不会由 Developer Save 自动写入 DB。角色入库后可在 Main/Characters 页修改 axis/sign 并 Apply。

### record

```json
{
  "revision": 44,
  "command": "record",
  "start": true
}
```

`start=false` 停止 recorder；`command=none` 清短时 active command。

## 3. Manager Scan

Developer 页 Scan：

1. 从新鲜 `runtime_status.character` 取得当前 `chr_id`；
2. 写 `command=bone_scan`；
3. Runtime 在主线程对当前 Animator root 遍历；
4. 写入：
   ```text
   SecondaryMotion/developer/bone_dumps/<chr_id>.json
   ```
5. Manager 每秒轮询并过滤显示骨骼名。

bone dump 当前为严格 JSON：

```json
{
  "character": "chr_xxxx_name",
  "bones": [
    { "name": "Root", "depth": 0 },
    { "name": "R_breast_01_jnt", "depth": 8 }
  ]
}
```

数组逗号和字符串 escaping 由独立 writer 处理。Scan 的 command 时机、旧 dump 清理和自动重试策略本轮未修改。

## 4. diagnostics.json

路径：

```text
SecondaryMotion/developer/diagnostics.json
```

示例：

```json
{
  "enabled": false,
  "modules": {
    "bone_scanner": false,
    "clip_inspector": false,
    "transform_recorder": false,
    "axis_tester": false,
    "hook_health": false
  },
  "axis_tester": {
    "test_angle_deg": 10,
    "axis": 2,
    "sign": 1
  }
}
```

该配置在 Runtime startup 加载，默认全部关闭；它不是普通用户的 preset，也不参与 `runtime/config.json` hot apply。

| 模块 | 行为/输出 |
|---|---|
| `bone_scanner` | arm 一次当前主控骨架扫描 |
| `clip_inspector` | 当前只加载并记录该 flag，没有直接 arm；使用 Developer `clip_inspect` command |
| `transform_recorder` | 直接启动 CSV recorder，不再要求 marker 或已打开文件 |
| `axis_tester` | 固定角度短时测试 |
| `hook_health` | `[HOOK-HEALTH]` hook 调用计数 |

## 5. Transform recorder

输出：

```text
SecondaryMotion/developer/breast_record.csv
```

列：

```text
frame,time_ms,gait,Rx,Ry,Rz,Rw,Lx,Ly,Lz,Lw
```

行为：

- 从 Runtime 数据根构造绝对路径，不依赖进程工作目录；
- 打开失败时 diagnostics config 每 2 秒重试；
- 成功后只启动一次；
- 最多 1800 frame，约 30 秒后 auto-stop；
- auto-stop 后不会因 config 仍为 true 而立即循环重开；
- Unity Transform 读取仍在主线程 frame path 中执行。

## 6. legacy marker compatibility

以下 txt 只属于旧测试/兼容通道，不是现行 Manager 正式工作流：

```text
bone_scan_test.txt
clip_inspect_test.txt
record_test.txt
axis_test.txt
spring_test.txt
amplify_test.txt
```

marker 目录不再硬编码盘符，而是从已加载 Runtime 路径推导：

```text
<game root>/SecondaryMotion
→ <game root>/plugin/<marker>.txt
```

worker 约 250 ms 检查并缓存；动画 callback 内没有 marker 文件 I/O。

- diagnostics 总开关关闭时，旧 bone/clip/record/axis marker 分支仍可兼容测试。
- `spring_test.txt` / `amplify_test.txt` 只有 `legacy_marker_mode=true` 时才门控正式写入；默认 false。

## 7. runtime_status.json

路径：

```text
SecondaryMotion/runtime/runtime_status.json
```

约 1 秒更新，字段：

| 字段 | 含义 |
|---|---|
| `state` | STARTING / CONFIG_LOADED / SYMBOLS_RESOLVED / HOOKS_INSTALLED / READY / DEGRADED / DISABLED_SAFE |
| `applied_revision` | 当前已安装 config snapshot revision |
| `character` | 当前规范化 `chr_id` |
| `profile` | 当前角色 profile 是否存在且可用 |
| `bones` | 左右骨骼是否解析成功 |
| `mode` | off / synthetic / amplify_native |
| `gait` | none / idle / walk / run / sprint / zipline |
| `last_write_ms` | 距最近骨骼 target 验证写入的时间 |
| `error` | 配置拒绝或 startup error 摘要；详细原因查日志 |

Manager 将 status 文件 5 秒内更新视为 Fresh。游戏未运行时，角色为空、Fresh=false 或 status 旧是正常离线状态，不应诊断为角色识别失败。

## 8. 日志

当前实际日志路径：

```text
<game root>/plugin/sbm_log.txt
<game root>/plugin/breast_probe_log.txt
```

Manager 日志在 Manager 程序目录：

```text
manager_startup.log
manager_changes.log
manager_crash.log
```

常用前缀：

| 前缀 | 含义 |
|---|---|
| `[REFLECT]` | IL2CPP 类型/字段/方法解析 |
| `[HOOK]` | required/optional hook 安装 |
| `[CFG]` | 配置 policy、字段拒绝、hot apply/ACK |
| `[CHAR]` | 主控身份与 profile |
| `[BONE]` | 显式/候选骨骼与 axis |
| `[GAIT]` | 20 Hz whitelist 分类 |
| `[PRE]` | synthetic angle 摘要 |
| `[DEVCMD]` | Developer command 消费 |
| `[BONE-SCAN]` | 骨架扫描 |
| `[REC]` | recorder 路径、进度和 auto-stop |
| `[MARKER]` | legacy marker cache |
| `[SHUTDOWN-DIAG]` | worker attach/detach 生命周期 |

## 9. 线程边界

允许在 worker 执行：

- 文件 I/O；
- JSON parse；
- status 写入；
- command/marker poll；
- plain flag/latch 更新。

必须留在主线程 hook：

- Animator clip 读取；
- Transform 遍历；
- Get/SetLocalRotation；
- bone scan、axis write、recorder frame capture。

不得在动画热路径新增文件 I/O、JSON parse、Sleep、managed allocation 或重复 Animator 全量扫描。

## 10. Synthetic motion 的层级合成边界（2026-09-05）

长窗口 recorder 已确认两类不同的周期性幅度变化：

### 胸骨 local 同轴拍频

弥弗连续 run 样本中：

- synthetic oscillator 稳定在约 `3.00003 Hz`；
- 胸骨 native local motion 约 `2.9829–2.9830 Hz`；
- 两者频差约 `0.0171 Hz`，对应约 `58.5 s` 的完整拍频周期；
- synthetic 自身没有 envelope 衰减，幅度低谷仍保留约 `2.99 Hz` 载波。

这说明 independent synthetic 若无条件叠加到当前 local pose 中已有的近频、同轴 native oscillator，会产生可见相消。该结论只适用于这一 synthetic 组合语义；不能据此笼统判定所有模式中的 `target = current * dq` 都错误。`amplify_native` 仍应保留并增强游戏原生 delta。

### 父骨链导致的视觉拍频

提弗洛斯 run 样本中：

- 胸骨 pre localRotation 近似静止，local synthetic target 也基本稳定；
- synthetic 约为 `2.700012 Hz`；
- 直接父骨 `Spine2` localRotation 的主频约 `1.494 Hz`，二次谐波约 `2.988–2.990 Hz`；
- `parentLocal × breastLocal` 的 body-relative composite 出现约 `0.2903 Hz` 包络，即 `3.444–3.445 s` 周期；
- 该周期与 `|2.989 - 2.700|` 的理论拍频一致；
- 层级重构 `grandWorld × parentLocal = parentWorld`、`parentWorld × breastLocal = breastWorld` 的最大误差约 `0.00024°`。

因此 recorder 中稳定的胸骨 local target 不等于最终视觉幅度稳定。最终画面还包含父骨链和 world-space 运动：

```text
breastWorld = ancestorWorld × parentLocal × breastLocal
```

### Filter 结论

当前保留单级 `τ=0.30 s` synthetic base filter。实验性两级 filter 虽进一步压低胸骨 local residual，但也可能移除原先对父链/world-space 运动的偶然补偿，使最终视觉周期反而更明显。

不要继续通过加重胸骨 local filter 来处理父链拍频。若未来必须消除父链影响，应作为独立的 ancestor/world-stabilized synthetic 语义设计和验证；直接抵消父骨运动会改变胸部跟随躯干的方式，不能作为最小 bugfix 全局启用。

## 11. 提弗洛斯梦境切换后直接翻转：当前证据边界（2026-09-05）

复现相关性：从正常世界进入梦境后触发，返回正常世界后恢复。

现有 probe 日志显示场景期间确实发生 Animator/角色实例重绑定，但每次重绑定后：

- `chr_id` 仍正确识别为 `chr_0034_typhoea`；
- 左右胸骨仍解析为 `breast_base_R_a_01_jnt` / `breast_base_L_a_01_jnt`；
- axis/sign 始终为 `axis=1 sign=+1`；
- 没有 config hot reload、runtime error、显式 reset 或 bone lookup failure 与翻转同时出现。

所附约 `12.75 s`、`gait=-1` CSV 中，排除复制时被截断的最后一行后，共 `765` 个有效 frame：

- 左右 quaternion norm 均约为 `0.999999–1.000001`；
- 无 quaternion 符号翻转；
- 最大相邻帧旋转分别约 `0.61°` 与 `0.54°`；
- 相对首帧最大变化约 `5.01°` 与 `4.42°`；
- 未记录到接近 `180°` 的 localRotation 跳变。

该 CSV 只有胸骨 localRotation，没有父骨/祖先 world rotation、Transform instance/parent identity 或切换瞬间前后配对样本。因此它不能判断翻转是否已被编码在新的父层级、bind pose、mesh/skinning 或场景专属后处理里。当前只能确认：翻转未表现为本插件所记录胸骨 localRotation 的突变，场景切换/重绑定相关性存在，但现有数据不足以定位因果。不得据此扩大 runtime 修改。

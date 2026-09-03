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

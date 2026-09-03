# 角色支持与入库指南

更新日期：2026-08-23

## 1. 角色支持模型

角色支持分成两层：

```text
SecondaryMotion/data/characters.default.json
  技术 DB：稳定 chr_id、显示名、骨骼、axis/sign、默认参数、animation_rules

SecondaryMotion/presets/<name>.json
  用户参数：enabled、motion_mode、axis/sign、五个 gait、envelope、jump、native factor
```

Runtime 只有在以下条件同时成立时才会写入：

1. 当前主控角色规范化后得到稳定 `chr_id`；
2. DB 中存在该角色；
3. profile 与全局开关 enabled；
4. 左右骨骼都找到；
5. 当前 gait 通过 locomotion whitelist；
6. 当前状态通过 Jump/target write gate。

未配置角色和单边骨骼配置均 fail closed，不会猜测并写入未知 Transform。

当前仓库 DB 和 Default preset 各包含 19 个角色；这是当前数据状态，不是 Runtime 上限。

## 2. 角色 ID 规范化

游戏对象名可能包含运行时后缀，例如：

```text
chr_0003_endminf_postmodel
chr_0003_endminf(Clone)
chr_0003_endminf#1
```

Runtime 规范化后使用稳定 ID：

```text
chr_0003_endminf
```

日志和 `runtime/runtime_status.json.character` 使用规范化 ID。显示名只用于 Manager UI；DB/preset 的键始终是稳定 `chr_id`，不能用本地化角色名替代。

## 3. 当前骨骼候选表

显式 DB 骨骼优先；允许 fallback 时按下列顺序搜索：

| 顺序 | Right | Left | 自动 axis |
|---:|---|---|---|
| 0 | `breast_R_01_jnt` | `breast_L_01_jnt` | Z |
| 1 | `R_breast_01_jnt` | `L_breast_01_jnt` | Z |
| 2 | `xiong_R_0_skin_jnt` | `xiong_L_0_skin_jnt` | Y |
| 3 | `breast_R_01` | `breast_L_01` | Z |
| 4 | `R_breast_01` | `L_breast_01` | Z |
| 5 | `xiong_R_0_skin` | `xiong_L_0_skin` | Y |

注意：

- 候选必须左右成对找到；只找到一侧视为失败。
- `spring_base_*_jnt` 是衣服/装饰弹簧骨，不应当作胸骨。
- xiong 通常使用 Y，其他候选通常使用 Z；DB/preset 显式 axis 可覆盖自动选择。
- 当前不再存在骨型统一倍率或 scale。不同角色的视觉差异直接通过每角色 gait Up/Down 调整。

## 4. Up / Down 语义

每个 gait 使用：

```json
{
  "amplitude_deg": 30,
  "amplitude_down_deg": 35,
  "frequency_hz": 2.7
}
```

- `amplitude_deg`：首半周期上摆，对应 Manager 的 Up。
- `amplitude_down_deg`：次半周期下摆，对应 Manager 的 Down。
- `amplitude_down_deg=0` 或缺失：Down 与 Up 对称。
- Main 和 Characters 两页均使用相同语义。

正式 gait：

```text
idle / walk / run / sprint / zipline
```

## 5. Manager Developer 入库流程

新角色不需要手工创建 marker 文件。正式流程通过：

```text
Manager Developer page
→ runtime/developer_command.json
→ Runtime 主线程执行 Scan
→ developer/bone_dumps/<chr_id>.json
→ Manager 读取结果
```

操作顺序：

1. 启动游戏并切到目标角色。
2. 确认 Developer 页已显示正确 `chr_id`；游戏未运行时显示 `-`/not detected 是正常离线状态。
3. 点击 Scan，等待 Runtime 生成该角色的 bone dump。
4. 用过滤器查看 `breast,xiong` 等候选，分别选择 Right 和 Left。
5. 可填写显示名。
6. 点击 Save。
7. Manager 重载 DB，并增加 `runtime/config.json.revision`，让 Runtime 热重载。
8. 回 Main/Characters 页调整 axis、sign、各 gait Up/Down/Frequency 和 envelope，再 Apply。

当前 Scan 实现保持单次 one-shot command + 每秒轮询 dump；本轮没有修改 Scan 时机、清旧 dump 或重试策略。若没有结果，应先确认游戏仍运行、当前角色已检测、Runtime status 新鲜，再决定是否重新点击；不要把离线 status 当成角色扫描故障。

## 6. Axis Test 的定位

Developer Axis Test 仍保留为可选诊断工具，但不是新角色入库必经步骤：

- 它只发送短时 `axis_test` Developer command；
- 测试结果不会由 Save 自动写入角色 DB；
- 正常工作流是在角色入库后，直接在 Main/Characters 页选择 axis/sign 并 Apply；
- 因此本轮没有实现 Axis Test 结果持久化，也没有把它加入 Save 的技术事实。

## 7. Save 行为

Developer Save 当前执行：

- 新建或更新 DB entry 的显示名、左右骨骼和 defaults；
- 保存五个 gait，包括 Zipline；
- 新角色复制 Aurora 在 Manager 当前合并模型中的 gait/envelope/mode/jump/native 参数；
- 已有角色重新 Save 时保留自身参数，不被 Aurora 覆盖；
- 已有 `animation_rules` 在重写 DB 时保留；
- 工具目录写入使用 `.bak` + atomic write；
- Save 后尝试镜像到游戏 `SecondaryMotion/data/characters.default.json`，随后 revision+1。

DB/preset 长期 source-of-truth、版本升级 merge 和用户数据覆盖策略仍是独立架构议题；本文只记录当前实现，不把现状描述成最终升级协议。

## 8. 角色专属动画规则

DB 可为角色增加 special/dash locomotion 映射：

```json
"animation_rules": {
  "actual_special_move_clip_fragment": "run",
  "actual_dash_clip_fragment": "sprint"
}
```

约束：

- gait 值只接受 `walk/run/sprint/zipline`；
- 最多 16 条，超过时整组拒绝并写配置日志；
- 多条同时命中时，最长 clip fragment 优先；
- 规则属于技术 DB，不属于用户 preset；
- 未配置规则的 special/dash/未知 clip 继续 fail closed。

## 9. 验收

Save/Apply 后至少确认：

- `runtime_status.json` 在 5 秒内持续刷新；
- `character` 是当前稳定 `chr_id`；
- `profile=true`；
- `bones=true`；
- `mode` 与 Manager 一致；
- `applied_revision` 等于 Manager 写入 revision；
- walk/run/sprint/zipline 分别测试，非 locomotion 状态不应继续写入。

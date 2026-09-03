# shutdown_detach_v1_result.md

**日期**：2026-08-17  
**实验**：IL2CPP Worker 生命周期单点实验（H1 验证）

## 版本 / hash

| 项 | 值 |
|---|---|
| source: plugin_main.h | 5443d68f732d5fbc |
| source: il2cpp_api.h | ebb776d30ecec0e3 |
| built DLL | 1e20f673f610c1948e8e250b681d6d9de6e36a0082a1e52675967e2e01aa57dd |
| deployed DLL | 1e20f673…（与 built 一致） |
| 上一版备份 | plugin/sbm.dll.shutdown_detach_v1 |

## 实验内容

单点改动：PluginWorker 在启动全部完成后执行 `il2cpp_thread_detach(attachedThread)`，worker 继续原 while(true) 服务循环。其余（Hook/Motion/Config/Log/DllMain）零改动。

前置静态审计：worker 服务循环仅 Win32/CRT/纯逻辑（marker 检查、status JSON、known-characters flush、diagnostics arm、Sleep），无任何 il2cpp_*/Unity API 调用 → 允许 detach。

## 日志证据

```
[SHUTDOWN-DIAG] il2cpp_thread_detach resolved PASS
[SHUTDOWN-DIAG] worker attached ptr=0x...
[SHUTDOWN-DIAG] startup complete ok=1
[SHUTDOWN-DIAG] worker IL2CPP DETACHED
[SHUTDOWN-DIAG] worker entering service loop detached
```

## 测试结果

- 功能回归：主控 synthetic motion 正常（walk/run/sprint 符合预期，无回归）
- 退出测试：游戏中 → 回登录界面 → 点击 Exit，正常退出，不再卡死

## Verdict

**PASS — H1 STRONGLY SUPPORTED**

永久 attached 的 PluginWorker 是游戏退出卡死的根因：
游戏点击 Exit 后进入 Unity/IL2CPP runtime teardown，worker 线程仍注册为
attached IL2CPP thread 且不退出，teardown 等待/枚举该线程 → 进程卡死。
detach 后 worker 退化为普通 Win32 线程，IL2CPP teardown 不再依赖它，
退出路径恢复畅通。

## 对 V2 的结论

1. 生命周期模式正式化：`attach → 初始化 → detach → 纯服务循环`。
2. worker 线程禁止持有 IL2CPP context；任何需要 IL2CPP 的周期性工作
   要么放主线程 hook，要么按需 attach/detach（需验证）。
3. 遗留项（不影响进程退出，V2 再议）：DLL_PROCESS_DETACH 无清理
   （hook 不卸载 / 日志不关 / 锁不销毁）。若未来支持游戏内卸载 DLL，
   必须先 MH_DisableHook + worker 退出后再 FreeLibrary，顺序不能反。

---

## 当前实现状态（2026-08-23）

以上正文是 2026-08-17 的历史单点实验记录；其中 source/DLL hash、备份文件名和“其余代码零改动”只描述当时实验版本，不能用于识别当前工作树或当前发布包。

现行 `src/plugin/plugin_main.h` 仍保留并正式使用：

```text
worker attach IL2CPP
→ PluginStartup 完成需要 IL2CPP 的初始化
→ il2cpp_thread_detach
→ detached Win32 service loop
```

当前 detached service loop 已扩展为：

- `developer_command.json` 轮询和 plain command state；
- `runtime/config.json` hot reload；
- `runtime_status.json` 写入；
- legacy marker cache；
- diagnostics flag/latch；
- discovered-character flush；
- Sleep。

需要 Unity/IL2CPP 对象的工作仍通过 main-thread hook 执行：Animator clip 读取、Transform 遍历、axis write、bone scan、recorder frame capture 都不在 detached worker 中直接调用。

当前 shutdown 结论保持：

1. worker 不能在永久 attached 状态进入无限服务循环；
2. detached 后不得直接执行 Unity/IL2CPP API；
3. 项目当前不是可热卸载 DLL：worker 仍为 `while(true)`，没有 cooperative stop；
4. `DLL_PROCESS_DETACH` 仍未实现完整 hook disable、worker join、日志/锁销毁；
5. 如果未来实现游戏内卸载，必须把它作为新的生命周期工程处理，不能把本实验的“正常退出”直接等同于“安全 FreeLibrary”。

本轮只进行了静态边界核对、109 项自动回归和 Runtime build；没有重新部署，也没有执行新的游戏退出实测。因此历史 PASS 仍是该实验的实测证据，本节只说明当前代码继续沿用其生命周期模式。

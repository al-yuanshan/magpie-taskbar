# 验证记录

日期：2026-11-06（Asia/Shanghai）。

## 已完成

- Debian WSL、GCC 12.2.0、CMake Release 构建路由解析测试成功；CTest 1/1 通过。测试使用显式断言函数，Release 模式不会关闭断言。
- 覆盖 agent 筛选、进行中/完成状态区分、fallback、空闲、实际模型选择、供应商前缀去重、分组原始请求保留、失败状态、排除本地会话及直连上报、参数编码、Unicode、非法 JSON 和响应结构错误。
- 实际调用本机 Magpie `0.1.3106` 的 `/api/hello`、`/v1/magpie/route-events/snapshot` 和带 `via=through&computer=this&limit=1` 的历史接口，全部 HTTP 200。
- 对实时响应只提取插件使用的字段后，将进行中快照和一条历史记录交给同一 C++ 解析实现，均成功解析；测试输出 `Live snapshot parsed: 1 routes`、`Live history parsed: 1 routes`。实时样本不随源码包分发。
- 独立静态复核 Windows API 类型、插件虚表接口、并发锁和线程池 DLL 引用生命周期，未发现可操作的问题。

## 尚未验证

- 环境没有 Visual Studio / MSVC / Windows SDK，因此没有 MSVC 版本。

## MinGW 版本（WSL 交叉编译）

- `build-mingw.sh` 用 Debian 的 `g++-mingw-w64-x86-64-posix`（GCC 12）编译为 `dist/mingw-x64/MagpieRoute.dll`（64 位，约 1.1 MB，静态链接 GCC 运行库，导出 `TMPluginGetInstance`）。
- 在 64 位 Windows PowerShell 中加载 DLL，并按 `PluginInterface.h` 的虚表顺序调用：`GetAPIVersion` 返回 8，插件名为 `Magpie Route`，数据刷新后显示项取值为 `Magpie: [last] cpa/claude-sonnet-5-5`，`Active: 0`，与本机 Magpie 状态一致。
- 这只验证了虚表布局和函数调用。MinGW 与 MSVC 的 C++ ABI 并不相同，尚未在真实 TrafficMonitor 中验证；如遇崩溃，请改用 MSVC 构建。
- 尚未在 TrafficMonitor 中验证加载、显示、悬停、配置刷新、卸载、不同 DPI 或不同 CPU 架构。
- HTTP 断连、超时、404 和 403 的界面表现目前经过代码检查，尚未在宿主内故障注入。

## 复现

跨平台模型测试：

```sh
cmake -S . -B build-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

Windows 完整编译与解析测试：在 VS 2022 Developer PowerShell 中运行 `./build.ps1 -Architecture x64`，再按 README 安装到 TrafficMonitor，检查上述宿主行为。

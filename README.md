# magpie-taskbar

在 [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) 任务栏显示 [Magpie](https://github.com/yetone/magpie) 网关的实时路由、额度、状态和用量。非官方插件，与 TrafficMonitor、Magpie 无隶属关系。

使用 TrafficMonitor 插件接口 API v8，独立 DLL，只读，只访问本机 `127.0.0.1`，不读取密钥、prompt 或回复内容。

## 显示项

在 TrafficMonitor 的显示项目中按需勾选，每项独立，文字统一大写：

| 项目 | 标签 | 含义 |
| --- | --- | --- |
| Magpie 模型 | `MDL` | 仅显示简化后的模型名，不附加状态或活动请求数量；没有模型时显示 `-` |
| Magpie 供应商 | `PRV` | 当前请求实际使用的供应商 |
| Magpie 路由分组 | `GRP` | 分组名；直接指定模型显示 `DIRECT`；信息缺失显示 `-` |
| Magpie 当前代理 | `AGT` | 当前或最近请求的 agent |
| Magpie 活动请求数 | `ACT` | 进行中的请求数 |
| Magpie 排队请求数 | `WAIT` | 等待槽位的请求数 |
| Magpie 额度 | `QTA` | 用量最高的额度窗口或余额 |
| Magpie 网关状态 | `STS` | 连接正常且没有活动请求异常时为 `OK`；活动请求降级为 `FALLBACK`、异常为 `ERR <状态码>`；连接失败为 `OFFLINE`。历史错误不影响当前状态 |
| Magpie 上次首字延迟 | `LAT` | 最近完成请求的首字延迟，计时缺失显示 `-` |
| Magpie 平均输出令牌速度 | `TPS` | 当前模型最近 5 条有效请求的令牌加权平均输出速度，单位 `T/S`；`~` 表示有效样本不足 3 条 |
| Magpie 今日费用 | `COST` | 今日费用，所有 agent 合计 |
| Magpie 今日令牌数 | `TOK` | 今日输入加输出令牌数 |

空闲时 `MDL`、`PRV`、`GRP`、`AGT` 显示最近完成的请求。`LAT` 来自最近完成请求。`TPS` 只统计与 `MDL` 相同模型的请求，取最近 20 条网关记录中最多 5 条有效样本，按总输出令牌 ÷ 总时长计算；计时从首个令牌（含思考）到请求结束。输出计时不足 1 秒或少于 64 令牌、流式时长不足输出时长 25%（集中返回）、超过 1000 T/S、隐藏思考且缺少思考令牌计数的请求不参与计算。悬停显示样本数、单条范围及各类排除原因。`TPS` 不是实时速度，也不等同于模型服务端生成速度；没有有效样本显示 `-`。

## 构建

需要 MSVC 以匹配 TrafficMonitor 的 C++ ABI。推荐使用本仓库的 GitHub Actions 构建，或在 Developer PowerShell 中执行：

```powershell
./build.ps1 -Architecture x64
```

生成 `dist/x64/magpie-taskbar.dll`。架构必须与 TrafficMonitor 一致（`x64`、`Win32`）。

也可在 WSL 用 MinGW 交叉编译：`bash build-mingw.sh`。MinGW 版只验证过虚表布局，未在真实 TrafficMonitor 中完整验证，出现崩溃请改用 MSVC 版。

## 安装与配置

1. 退出 TrafficMonitor，将匹配架构的 `magpie-taskbar.dll` 放入其 `plugins` 文件夹。
2. 启动 TrafficMonitor，在插件管理中确认已加载。
3. 在插件管理中点击 magpie-taskbar 的“选项”，在设置窗口中修改端口、刷新间隔、代理筛选、精简模型名称及各显示项字符数；可“测试连接”检查 Magpie，代理下拉列表来自最近的网关请求。点击“确定”保存到 `magpie-taskbar.ini`（旧版 `MagpieRoute.ini` 会自动复制）。
4. 设置窗口保存后立即刷新数据，并通知 TrafficMonitor 设置已更改；若任务栏宽度未更新，重启 TrafficMonitor。也可点击“打开配置文件”直接编辑 ini，手动编辑后需重启。示例见 `magpie-taskbar.ini.example`，各项长度见 [CHARACTER-SETTINGS.md](docs/CHARACTER-SETTINGS.md)。

`Agent` 留空表示所有 agent；设置为 `codex`、`claude` 等可筛选。`RefreshMs` 范围 1000–60000，`Port` 默认 3425。

## 使用的接口

- `/v1/magpie/route-events/snapshot`：进行中的请求
- `/v1/magpie/usage/requests`：最近完成的请求
- `/v1/magpie/concurrency`：排队请求
- `/v1/magpie/quotas`：额度
- `/v1/magpie/usage?period=today`：今日用量

请求禁用代理和跳转，有超时和 2 MiB 响应限制。旧版 Magpie 没有这些接口时显示不可用。

## 测试

```sh
cmake -S . -B build-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

验证记录见 [VALIDATION.md](docs/VALIDATION.md)。

## 许可证

本项目自身代码使用 [MIT](LICENSE)。

`third_party/TrafficMonitor` 下的插件接口头文件来自 TrafficMonitor，遵循 "Anti 996" 许可证（含劳动法合规条款），不是 MIT。再分发时请原样保留该目录下的 `LICENSE` 和头文件中的版权声明；发布 DLL 时请一并附上 `LICENSE` 和 `docs/THIRD_PARTY_NOTICES.md`。

其他第三方组件及其许可证见 [THIRD_PARTY_NOTICES.md](docs/THIRD_PARTY_NOTICES.md)。

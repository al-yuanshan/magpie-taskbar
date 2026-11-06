# magpie-taskbar

在 [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) 任务栏显示 [Magpie](https://github.com/yetone/magpie) 网关的实时路由、额度、状态和用量。非官方插件，与 TrafficMonitor、Magpie 无隶属关系。

使用 TrafficMonitor 插件接口 API v8，独立 DLL，只读，只访问本机 `127.0.0.1`，不读取密钥、prompt 或回复内容。

## 显示项

在 TrafficMonitor 的显示项目中按需勾选，每项独立，文字统一大写：

| 项目 | 标签 | 含义 |
| --- | --- | --- |
| Magpie 模型 | `MDL` | 简化后的模型名；有进行中请求时前面带 `*`，并行时后接数量 |
| Magpie 供应商 | `PRV` | 当前请求实际使用的供应商 |
| Magpie 路由分组 | `GRP` | 分组名；直接指定模型显示 `DIRECT`；信息缺失显示 `-` |
| Magpie 当前代理 | `AGT` | 当前或最近请求的 agent |
| Magpie 活动请求数 | `ACT` | 进行中的请求数 |
| Magpie 排队请求数 | `WAIT` | 等待槽位的请求数 |
| Magpie 额度 | `QTA` | 用量最高的额度窗口或余额 |
| Magpie 网关状态 | `STS` | 连接正常且没有活动请求异常时为 `OK`；活动请求降级为 `FALLBACK`、异常为 `ERR <状态码>`；连接失败为 `OFFLINE`。历史错误不影响当前状态 |
| Magpie 上次首字延迟 | `LAT` | 最近完成请求的首字延迟，计时缺失显示 `-` |
| Magpie 上次输出令牌速度 | `TPS` | 最近完成请求的输出速度，单位 `T/S` |
| Magpie 今日费用 | `COST` | 今日费用，所有 agent 合计 |
| Magpie 今日令牌数 | `TOK` | 今日输入加输出令牌数 |

空闲时 `MDL`、`PRV`、`GRP`、`AGT` 显示最近完成的请求。`TPS`、`LAT` 来自最近完成请求，与当前活动请求可能不同；悬停查看所属 agent、模型、请求时间、总耗时及结果。`TPS` 不是实时速度。

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
3. 在插件选项中打开 `magpie-taskbar.ini`，首次打开会自动创建（旧版 `MagpieRoute.ini` 会自动复制）。示例见 `magpie-taskbar.ini.example`，各项长度见 [CHARACTER-SETTINGS.md](CHARACTER-SETTINGS.md)。
4. 修改字符数后需重启 TrafficMonitor 才会重新计算宽度。

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

验证记录见 [VALIDATION.md](VALIDATION.md)。

## 许可证

本项目自身代码使用 [MIT](LICENSE)。

`third_party/TrafficMonitor` 下的插件接口头文件来自 TrafficMonitor，遵循 "Anti 996" 许可证（含劳动法合规条款），不是 MIT。再分发时请原样保留该目录下的 `LICENSE` 和头文件中的版权声明；发布 DLL 时请一并附上 `LICENSE` 和 `THIRD_PARTY_NOTICES.md`。

其他第三方组件及其许可证见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

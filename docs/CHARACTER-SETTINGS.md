# 各显示项长度配置

推荐在 TrafficMonitor 插件管理中打开 magpie-taskbar 的“选项”，直接在设置窗口中修改，输入范围会自动校验。设置窗口对应 `magpie-taskbar.ini` 中 `[Magpie]` 下的以下配置，也可点击“打开配置文件”手动编辑：

```ini
[Magpie]
Port=3425
Agent=
RefreshMs=2000
CompactName=1
ModelCharacters=14
ProviderCharacters=8
GroupCharacters=16
ActiveCharacters=3
QuotaCharacters=12
StatusCharacters=10
WaitingCharacters=3
LatencyCharacters=6
CostCharacters=8
TokenCharacters=9
AgentCharacters=10
SpeedCharacters=9
```

- ModelCharacters：模型内容长度，默认 14。
- ProviderCharacters：供应商内容长度，默认 8。
- GroupCharacters：分组内容长度，默认 16。
- ActiveCharacters：请求数量内容长度，默认 3。
- QuotaCharacters / StatusCharacters / WaitingCharacters / LatencyCharacters / CostCharacters / TokenCharacters：额度、状态、排队、延迟、今日费用、今日 token 的内容长度。

## 显示项说明

| 项目 | 标签 | 来源与含义 |
| --- | --- | --- |
| Magpie Agent | `AGT: ` | 最新活动请求的 agent 名称，大写显示；空闲时显示最近完成请求的 agent，缺失显示 `-`。AgentCharacters 控制长度，默认 10，最大 100 |
| Magpie Output Speed | `TPS: ` | 当前模型最近 5 条有效请求的令牌加权平均输出速度，如 `152.3T/S`；计时从首个令牌到请求结束。小样本、集中返回、超过 1000 T/S 及隐藏思考缺少计数的请求不参与计算；有效样本不足 3 条标记 `~`。没有有效样本显示 `-`。悬停列出样本数、单条范围及排除原因。SpeedCharacters 默认 9，最大 20 |
| Magpie Group | `GRP: ` | 分组调用显示大写分组名；直接指定模型显示 `DIRECT`；请求信息或分组名缺失时显示 `-`。有活动请求时取最新活动请求，空闲时取最近完成请求 |
| Magpie Quota | `QTA: ` | `/v1/magpie/quotas?kept=1` 中最近使用的账号；有限额窗口时显示用量最高的窗口，如 `WEEK 72%`；否则显示余额 |
| Magpie Status | `STS: ` | 当前连接及活动请求状态：`OK`、`FALLBACK`、`ERR 429` 或 `OFFLINE`，历史失败不影响当前状态；历史结果见悬停 |
| Magpie Waiting Requests | `WAIT: ` | `/v1/magpie/concurrency` 中排队等待槽位的请求数 |
| Magpie Latency | `LAT: ` | 上次完成请求的首字延迟，缺失时显示 `-`；总耗时在悬停中单独显示 |
| Magpie Cost Today | `COST: ` | 今日费用，来自 `/v1/magpie/usage?period=today`，为所有 agent 合计，不受 Agent 筛选影响 |
| Magpie Tokens Today | `TOK: ` | 今日输入加输出 token，同样为所有 agent 合计 |

额度每 15 秒、用量每 30 秒刷新一次，路由和排队按 RefreshMs 刷新。

模型、供应商和分组可设 1–100，请求数量可设 1–10。不包含 `MDL: ` 等标签。超长内容使用省略号，悬停查看完整详情。字符数同时用于预留显示宽度；字体为比例字体时，实际像素宽度不等同于字符数。旧 `MaxCharacters` 仍作为模型项的兼容默认值，新的 ModelCharacters 优先。

通过设置窗口保存后立即应用截断限制并通知 TrafficMonitor 设置已更改；若占位宽度未更新，或是手动编辑了 ini，请重启 TrafficMonitor。

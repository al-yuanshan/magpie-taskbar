# Magpie Route · TrafficMonitor 插件

在 TrafficMonitor 任务栏显示 Magpie 网关的实时路由、额度、状态和用量。非官方插件，只读，只访问本机 `127.0.0.1`。

## 安装

1. 按 TrafficMonitor 的位数选择压缩包（x64 或 Win32），位数必须一致。
2. 退出 TrafficMonitor，把 `MagpieRoute.dll` 放入其 `plugins` 文件夹。
3. 启动 TrafficMonitor，在显示项目中勾选需要的 `Magpie ...` 项目。

插件选项会打开 `MagpieRoute.ini`（首次自动创建），可设置端口、agent 筛选、刷新间隔和各项显示长度。修改长度后需重启 TrafficMonitor。

完整说明、配置项和问题反馈：https://github.com/al-yuanshan/magpie-taskbar

## 许可证

本项目为 MIT 许可，见 `LICENSE`。内含的 TrafficMonitor 插件接口使用 Anti-996 许可证，全文见 `THIRD_PARTY_NOTICES.md`，请保留。

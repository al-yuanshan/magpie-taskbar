# magpie-taskbar · TrafficMonitor 插件

在 TrafficMonitor 任务栏和主窗口显示 Magpie 网关的实时路由、额度、状态和用量。非官方插件，只读，只访问本机 `127.0.0.1`。

## 安装

1. 按 TrafficMonitor 的位数选择压缩包（x64 或 Win32），位数必须一致。
2. 退出 TrafficMonitor，把 `magpie-taskbar.dll` 放入其 `plugins` 文件夹。
3. 启动 TrafficMonitor，在显示项目中勾选需要的 `Magpie ...` 项目。

插件“选项”会打开设置窗口，可设置端口、agent 筛选、刷新间隔、精简模型名称和各项显示长度，并可测试与 Magpie 的连接。若修改长度后任务栏宽度未更新，请重启 TrafficMonitor。

完整说明、配置项和问题反馈：https://github.com/al-yuanshan/magpie-taskbar

## 主窗口皮肤

1. 将随附的 `skins/Magpie` 文件夹复制到 TrafficMonitor 的 `skins` 目录，保留文件夹层级。
2. 在主窗口设置中选择 `Magpie` 皮肤。仅安装 DLL 不会给内置皮肤自动增加插件布局。
3. 皮肤包含上传、下载、CPU、内存和 10 个 Magpie 指标，共 14 项；大小窗口布局相同，无需开启“显示更多信息”。活动请求数与排队请求数可在任务栏中单独勾选。

皮肤使用灰色顶部标签和亮色数值。升级时覆盖整个皮肤文件夹，再重新选择皮肤或重启 TrafficMonitor。若字体或颜色没有更新，请恢复皮肤默认字体与颜色。

## 许可证

本项目为 MIT 许可，见 `LICENSE`。内含的 TrafficMonitor 插件接口使用 Anti-996 许可证，全文见 `THIRD_PARTY_NOTICES.md`，请保留。

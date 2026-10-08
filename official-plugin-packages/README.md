# Native Release 官方插件包

`official-plugin-packages/<插件 ID>/<版本>/` 是官方插件包的来源。Native Release 发布时，
工作流运行 `tools/pack-official-plugins.ps1`，生成 `<ID>-<版本>.ssmtpkg`
与 `ssmt-plugin-catalog.json` 并上传到同一 Release。
版本目录中可放 `release-status.json` 并设置 `"publish": false` 暂缓发布。
当前旧 DLSS5 Swapper 桥接版暂缓发布，避免把需要玩家手工准备外部 DLL 的路线
当作 SSMT 自主管理资源的完成品。

目录中的清单只能声明该包实际提供的能力。外部依赖仍需由 SSMT 的图形资源管理器
取得与安装；不能因为插件包发布成功，就把外部 DLSS5 DLL 标为已安装。

本地验证可在 PowerShell 中运行：

```powershell
./tools/pack-official-plugins.ps1 -ReleaseTag v-test -OutputDirectory ./dist/plugin-release-test
```

输出目录必须为空。正式发布使用真实 Release tag，目录中的资产名称、大小和 SHA-256
需与上传后的 Release 一致；插件市场会再次核对这些字段。

含 `runtimePlugins` 的包会从 `dist/Release`（可用 `-NativeArtifactDirectory` 指定）补入
已编译 DLL。打包器在临时目录组装，缺少声明的 DLL 时失败，源码目录不保存编译产物。

原神运行时探针源码位于 `SSMT-Poser/`，已完成当前样本的 native resolver、Rig / Actor
枚举和可见的单骨旋转 / 恢复；新增 VMD FK 旋转预览已通过本地测试，实机动作验证待完成。
`release-status.json` 将研究版本排除在正式目录之外；不会随普通官方打包自动发布。
本地测试可使用 `-DevelopmentPluginId ssmt.poser.genshin-probe`，生成
`dev.ssmt.poser.genshin-probe` 包并通过现有第三方包入口校验、确认、安装。
开发包不生成官方目录，不改变 `ssmt.*` 只能来自 Native Release 的来源规则。
显式开发打包可以包含 `publish: false` 的实验版本；发布状态文件只用于构建，不装入插件包。

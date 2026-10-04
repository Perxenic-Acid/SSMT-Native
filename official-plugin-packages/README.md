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

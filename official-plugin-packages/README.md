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
开发包不生成官方目录，普通“从文件安装”仍拒绝未验证来源的 `ssmt.*` ID。
显式开发打包可以包含 `publish: false` 的实验版本；发布状态文件只用于构建，不装入插件包。

## build_native.ps1 本地构建发现

主仓库 `build_native.ps1` 默认同时构建 Poser，可用 `-BuildPoserProbe $false` 跳过。
构建后调用 `tools/stage-local-plugins.ps1`，从本目录的清单与本次 dist 产物生成
`src-tauri/resources/PluginPackages/*.ssmtpkg`。没有清单的裸 DLL / 宿主测试 DLL 不作为市场插件。
缺少 runtime DLL 的可选插件跳过，纯本体 / 外部工具插件可直接打包。

市场自动只读发现此目录，即使 Native Release 请求失败也可显示本地插件。
本地构建保留原 ID，便于复用已有插件贡献；标注“本地构建”，不声明 Native Release 官方来源。
安装前展示文件路径、清单、权限与免责声明，安装时重新发现并检查所审阅 SHA256，
通过现有包校验 / 安装器落盘。默认不启用，不修改游戏注入配置。
本地安装命令只接受此受控目录重新发现的包，不允许任意路径绕过保留 ID 限制。

`pack-official-plugins.ps1 -DevelopmentPluginId <ID> -LocalBuild` 为此本地管线保留 ID；
不生成官方 Release 目录，也不发布。未加 `-LocalBuild` 的原有 dev ID 打包方式保持不变。

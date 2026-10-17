# Native Release 官方插件包

`official-plugin-packages/<插件 ID>/<版本>/` 是官方插件包的来源。Native Release 发布时，
工作流独立构建所需 Native 产物，再运行 `tools/pack-official-plugins.ps1`，生成 `<ID>-<版本>.ssmtpkg`
与 `ssmt-plugin-catalog.json` 并上传到同一 Release。
每个版本必须有 `release-status.json`，`"publish": true` 才会进入正式 Release。
当前仓库所有 7 个版本均已允许发布，包括旧 DLSS5 Swapper 路线及 Poser。
较旧版本的外部依赖与实验性限制仍需在安装说明中明确，不代表已获得完整游戏验收。

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

非 Plugin ABI 的辅助组件可由 `build-artifacts.json` 声明构建产物与包内相对路径，
打包时一并生成 SHA-256 旁置文件；该构建清单不装入包。DLSS5 0.2.1 使用此方式
携带 SSMT 适配的 GIMI Bridge 与许可证，不将辅助 DLL 声明为 `runtimePlugins`。

原神运行时探针源码位于 `SSMT-Poser/`，已完成当前样本的 native resolver、Rig / Actor
枚举和可见的单骨旋转 / 恢复；新增 VMD FK 旋转预览已通过本地测试，实机动作验证待完成。
Poser 已纳入正式发布：CI 必须构建、测试 `SSMT-Poser.dll`，并将其装入独立 `.ssmtpkg`。
本地测试可使用 `-DevelopmentPluginId ssmt.poser.genshin-probe`，生成
`dev.ssmt.poser.genshin-probe` 包并通过现有第三方包入口校验、确认、安装。
开发包不生成官方目录，普通“从文件安装”仍拒绝未验证来源的 `ssmt.*` ID。
显式开发打包可以包含 `publish: false` 的实验版本；发布状态文件只用于构建，不装入插件包。

## build_native.ps1 本地构建发现

主仓库 `build_native.ps1` 默认同时构建 Poser，可用 `-BuildPoserProbe $false` 跳过。
构建后调用 `tools/stage-local-plugins.ps1`，从本目录的清单与本次 dist 产物生成
主仓库 `build/plugin-packages/*.ssmtpkg`。没有清单的裸 DLL / 宿主测试 DLL 不作为市场插件。
缺少 runtime DLL 的可选插件跳过，纯本体 / 外部工具插件可直接打包。
未声明 `runtimePlugins` 不等于缺少 DLL；PowerShell 管线须过滤缺省字段产生的空值，
辅助组件仍按 `build-artifacts.json` 独立检查。

仅 Debug 应用只读发现此开发目录，即使 Native Release 请求失败也可显示开发包。
正式版不扫描开发包或旧版 `resources/PluginPackages`，安装器不携带插件包、旧占位清单
或 Player Tweaks DLL；仅通过 Native Release 目录按需安装官方插件。第三方文件安装
继续保留安全检查与来源、权限确认。已经独立安装到全局 Plugins 目录的包不受影响。
本地构建保留原 ID，便于复用已有插件贡献；标注“本地构建”，不声明 Native Release 官方来源。
安装前展示文件路径、清单、权限与免责声明，安装时重新发现并检查所审阅 SHA256，
通过现有包校验 / 安装器落盘。默认不启用，不修改游戏注入配置。
本地安装命令只接受此受控目录重新发现的包，不允许任意路径绕过保留 ID 限制。

本体贡献迁移版本：DLSS5 0.2.2、Shade 0.1.1、Player Tweaks 0.1.0。
这些包通过 `hostExtensions` 携带路线、资源位置与功能声明；由新版 SSMT 中已注册
provider 执行，不加载清单中的任意代码。Player Tweaks 的原神 Hook 作为辅助产物随包
分发，崩铁只使用客户端设置后端，不注入该 DLL。Player Tweaks 不再有本体内置的
虚拟安装记录或资源回退；两种后端都要求插件包已安装并对当前游戏启用。
迁移版本已经允许正式发布；每个版本作为独立的 `.ssmtpkg` 下载资产。
打包成功不代表游戏内效果或长期稳定性已经验收。

`pack-official-plugins.ps1 -DevelopmentPluginId <ID> -LocalBuild` 为此本地管线保留 ID；
不生成官方 Release 目录，也不发布。未加 `-LocalBuild` 的原有 dev ID 打包方式保持不变。

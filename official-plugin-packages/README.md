# Native Release 官方插件包

`official-plugin-packages/<插件 ID>/<版本>/` 是官方插件包的来源。Native Release 发布时，
工作流独立构建所需 Native 产物，再运行 `tools/pack-official-plugins.ps1`，生成 `<ID>-<版本>.ssmtpkg`
与 `ssmt-plugin-catalog.json` 并上传到同一 Release。
每个版本必须有 `release-status.json`，`"publish": true` 才会进入正式 Release。
各版本的发布资格以状态文件为准，包括旧 DLSS5 Swapper 路线及 Poser。
较旧版本的外部依赖与实验性限制仍需在安装说明中明确，不代表已获得完整游戏验收。

插件自身版本与 Native Release 标签相互独立。客户端查询 GitHub `/releases/latest` 的目录，
再按插件清单版本决定是否更新；重编或换 Release 标签不会让同一 `ID@版本` 成为新版本。
改变插件实现时必须新增更高版本目录；不能把新 DLL 重新塞入已发布的旧版本。
Poser 当前为 `0.1.1`，旧 `0.1.0` 已撤销后续重打资格，历史 GitHub 资产不修改。
其 DLL 查询版本与清单共用版本来源。

`release-status.json` 可声明仓库相对的 `sourcePaths`，覆盖插件源码和共享构建依赖。
正式发布时 `tools/check-plugin-release-versions.ps1 -BaseRef <GitHub 最新正式 Release 标签>`
检查这些路径及包目录的 Git 改动；实现已改变而最高获准版本未提高时拒绝发布，
读取发布基线失败也不能继续。当前先为 Poser 启用；其他插件仍须遵守相同升版约定。
构建测试模式不会自动创建标签或发布版本，正式发布仍须显式选择 `publish_new_release`。

目录中的清单只能声明该包实际提供的能力。外部依赖仍需由 SSMT 的图形资源管理器
取得与安装；不能因为插件包发布成功，就把外部 DLSS5 DLL 标为已安装。

本地验证可在 PowerShell 中运行：

```powershell
./tools/pack-official-plugins.ps1 -ReleaseTag v-test -OutputDirectory ./dist/plugin-release-test
```

输出目录必须为空。正式发布使用真实 Release tag，目录中的资产名称、大小和 SHA-256
需与上传后的 Release 一致；插件市场会再次核对这些字段。
清单缺省的可选数组在目录中输出 `[]`，不能使用产生 `[null]` 的空值转换；
单个版本的非法依赖也会导致客户端拒绝整个目录。v0.1.6 曾发布这种空依赖表示，
SSMT 客户端在验证目录原始字节后提供窄范围兼容，后续发布由打包器直接输出正确数组。

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

Secondary Motion 3.2.1 通过固定上游提交
`Sp1cHless/Arknights-Endfield-Plugin-Secondary-bodyphysics@90266f837dde5822a6a47b7403cc64081e3a9368`
构建 `sbm.dll`。Windows 工作流调用 `tools/build-secondary-motion.ps1` 后，打包器将实际
产物放入 `plugin/sbm.dll`，并根据该字节流回写清单中的 SHA-256。包内同时保留上游
GPL-3.0 许可证、第三方声明、来源记录、角色数据和 Default/Set1/User 预设。
3.2.0 因包内清单包含 SSMT 不支持的 `description` 字段而导致安装失败，已从正式发布
候选中撤销；3.2.1 删除该字段并新增打包前的清单字段白名单校验。

插件 PR 与 `main` 变更会运行独立的 `Validate official plugin packages` 工作流：
校验所有获准清单的顶层与嵌套字段，执行打包/版本回归，固定上游提交构建
`sbm.dll`，再从实际 `.ssmtpkg` 中读取清单并核对 DLL 哈希。正式发布工作流仍采用
显式 `publish_new_release`，PR 校验不会上传或发布资产。

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

## 原神 OptiScaler 中文面板

DLSS5 0.2.3 携带汉化 OptiScaler 辅助组件。源码与固定提交见
`tools/sources/optiscaler.json`；保留现用 GIMI 补丁，不切换渲染后端。
Native CI 检出固定源码、运行无图形设备的界面回归并编译，再把 DLL、GPL 许可证、
第三方声明及来源记录装入该插件包。发布前必须先将固定提交推送到公开 fork，
让消费者可取得相应源码；CI 无法检出该提交时应失败，不回退到最新分支或旧 DLL。

本地开发按顺序执行（仅构建组件，不安装到游戏目录）：

```powershell
./tools/build-optiscaler.ps1 -SourceDirectory C:/path/to/OptiScaler_DLSSNR
./tools/stage-local-plugins.ps1 -NativeArtifactDirectory ./dist/Release -OutputDirectory ../build/plugin-packages
```

本地发现按版本检查产物，未构建 OptiScaler 时仅跳过 0.2.3，不屏蔽已有的旧版本。
正式打包仍要求所有获准版本的辅助产物完整。打包回归入口为
`tools/tests/test-official-packaging.ps1`。
新版 SSMT 只在已安装且为原神启用该插件时，将汉化组件暂存到自有热注入目录；
其他游戏及 Feeder 路线不受此 DLL 影响。旧插件继续原组件，完整游戏内效果待实机验收。

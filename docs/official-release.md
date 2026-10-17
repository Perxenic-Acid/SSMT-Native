# SSMT Native 官方构建与发布

使用 [Build and publish SSMT Native](../.github/workflows/official-plugins.yml)（Actions → Run workflow）。

## 构建与发布

- **release_mode = build_only（默认）**：编译 Native、Bridge、Rust Plugin Host、Player Tweaks 与 Poser，执行 ABI、CameraPolicy 及 Poser CTest；生成独立插件包与发布候选文件，仅存储 Actions Artifact，不创建 Tag 或 Release。此模式的插件目录下载链接不可直接使用（`ci-<run id>` 是临时标签）。
- **release_mode = publish_new_release**：只能从 `main` 启动，需填写尚不存在的版本号（如 `v0.1.6`）。构建与测试通过、独立插件包生成后，先创建附带完整资产的草稿 Release，再设为公开。拒绝覆盖已有 Tag、Release 或资产。
- **Poser 必建**：Poser 已列入公开发布范围；不再有可跳过它的构建选项。源码及测试仍处于迭代中，但不因此停止分发。

## 发布哪些文件？

GitHub Release 为每一项提供**独立下载**，无需下载整套编译产物：

| 资产 | 用途 |
| --- | --- |
| `Run.exe` | 3DMigoto Native 注入器，可单独下载 |
| `ssmt_plugin_host.dll` | Rust Plugin Host，可单独下载 |
| `SSMT-Native-windows-x64.zip` | 上述两个核心文件的兼容合集，方便已有流程使用 |
| `ssmt.dlss5.integration-<版本>.ssmtpkg` | 每个版本独立的 DLSS5 插件包 |
| `ssmt.hoyoshade.bridge-<版本>.ssmtpkg` | 每个版本独立的 HoYoShade 插件包 |
| `ssmt.player-tweaks-<版本>.ssmtpkg` | Player Tweaks 插件包，包含其需要的 Native DLL |
| `ssmt.poser.genshin-probe-<版本>.ssmtpkg` | Poser 插件包，包含 `native/SSMT-Poser.dll` |
| `ssmt-plugin-catalog.json` | 官方插件目录：各版本独立 URL、大小和 SHA-256 |
| `SHA256SUMS.txt` | Release 资产的 SHA-256 校验和 |

具体插件版本以 `official-plugin-packages/` 的已批准目录为准。DLSS5 的辅助 GIMI Bridge 作为对应插件包的 `bridge/` 内容分发；它不是 Plugin ABI DLL，不能放进 `runtimePlugins` 自动注入。

**无需下载 Native ZIP 才能安装某个插件**。插件市场应读取目录 JSON 中对应版本的 `downloadUrl`，只下载需要的 `.ssmtpkg`。注意某些版本依赖本地已有的外部工具、游戏配置或图形驱动；公开发布不等于这些条件自动满足。

## 发布状态

每个插件版本都有 `official-plugin-packages/<ID>/<版本>/release-status.json`。其中 `"publish": true` 表示可加入官方插件目录与 Release，`"publish": false` 仅用于明确需要停止分发的版本；无文件也不发布。

本次已为仓库现有全部 7 个版本显式启用 `publish: true`：DLSS5 0.2.0 / 0.2.1 / 0.2.2、HoYoShade Bridge 0.1.0 / 0.1.1、Player Tweaks 0.1.0、Poser 0.1.0。**仍在测试与可以公开下载并不矛盾**。对于旧版本或尚未全面实机验证的功能，继续在说明文档中告知实际成熟度，不以隐藏下载替代提示。

打包时检查清单中声明的 Native DLL 是否已编译。缺失即失败，而不是发布残缺插件；插件目录条目数量须与生成的 `.ssmtpkg` 数量一致。

## Actions Artifacts 与正式 Release 的区别

- `SSMT-Native-release-assets`：供正式 Release 上传的所有独立发布资产；构建测试时亦生成候选文件。
- `SSMT-Native-test-binaries`：仅供调试用的完整 `dist/Release/` 原始二进制，可能包含中间组件；不作为用户安装渠道，也不上传 GitHub Release。

## 发布失败恢复

若工作流已创建草稿但上传或公开步骤失败，可能留下草稿及 Tag。默认不复用已有 Tag，请先人工核对资产及状态后再处理。发布成功之前，构建通过不等于已经创建了新 Release。

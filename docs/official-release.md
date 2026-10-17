# Native 官方构建与发布

使用 [Build and publish SSMT Native](../.github/workflows/official-plugins.yml) 工作流（Actions → Run workflow）。

## 手动运行

- **release_mode = build_only（默认）**：构建 Native、Bridge、Rust 插件 Host，执行 ABI 与既有自动化测试；只产生 Actions Artifacts，不创建 Git Tag 或 GitHub Release。release_tag 此时被忽略。
- **release_mode = publish_new_release**：必须从 `main` 运行，并输入**不存在**的版本标签（例如 `v0.1.2`）。全部构建和指定测试通过后，先生成带资产的草稿 Release，上传成功后再公开发布；不会覆盖已有 Tag、Release 或资产。
- **build_poser = true**：额外构建与测试实验性 Poser；`SSMT-Poser.dll` 仅进入测试用 Artifact，不会因此取得正式发布资格。

此工作流不会在其他人手工发布 Release 时自动执行，也不会自行替已有 Release 补上传资产。

## 官方插件发布资格

每个版本的 `official-plugin-packages/<插件 ID>/<版本>/release-status.json` 控制独立 `.ssmtpkg` 是否允许进入官方目录。

必须显式写入布尔值：

```json
{ "publish": true }
```

无此文件、`publish: false`、或非布尔值时均不发布。新增插件与旧版本一视同仁。测试用 `-DevelopmentPluginId` 打包不改变这一规则。**构建成功和可发布是两项独立的判断**。

如果当前没有获准发布的插件，则正式插件目录包含零条目，但 Native 核心版本依然可以正常发布。不要为了填充目录而解除实验插件的暂缓发布状态。

## 产物隔离

- **SSMT-Native-release-assets**（Actions Artifact；可创建 Release）：`SSMT-Native-windows-x64.zip`（仅 `Run.exe` 与 `ssmt_plugin_host.dll`）、`ssmt-plugin-catalog.json`、`SHA256SUMS.txt`、以及被显式批准的 `.ssmtpkg`。
- **SSMT-Native-test-binaries**（仅 Actions Artifact；不上传 Release）：`dist/Release/` 中所有编译得到的 Native 二进制，可能包含暂缓发布的 Player Tweaks、Gimi Bridge 和 Poser。

正式 Release 只上传第一个 Artifact 的内容。启用 Poser 构建，不会偷偷改变正式 ZIP 或插件目录的内容。

## 故障处理

发布任务若在创建草稿之后失败，可能留下未公开的草稿 Release 或关联 Tag。工作流默认拒绝复用标签；需人工检查并清理后才可再次尝试。已有的 Release 资产不会被覆盖。

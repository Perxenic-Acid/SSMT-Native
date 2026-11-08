# DLSS5 独立后台开发包

设置页面、下载与导入、组件历史、路线检查、启动计划和旧状态恢复全部由包内后台实现。页面通过宿主通用租约桥调用自身后台；本体不识别 DLSS5、ReShade、Swapper 或显卡策略。

本包要求 Plugin API 1.1。与其他图形插件的共享协议为 `ssmt.reshade.v1`，由插件自己解释；资源竞争与失败回退由宿主通用启动协议处理。禁用贡献后下次准备会移除其效果和 Add-on，同时保留用户配置。

使用 `tools/build-plugin-backends.ps1 -Backends dlss5` 构建后台与页面，再通过 `tools/pack-official-plugins.ps1` 按 `build-artifacts.json` 打包。后台、DLL、JS、CSS 的构建产物不存入此来源目录，打包时更新后台哈希。

当前 `publish: false`。内存、子进程和离线包校验不代表真实游戏或显卡验收；正式发布前需要验证单插件、多插件、全部启用、停用恢复和游戏内效果。

# Player Tweaks 统一配置页开发包

要求 Plugin API 1.2。配置描述、游戏差异和保存逻辑由包内后台提供；宿主根据 `ssmt-config/1` 解码为统一主题控件，不需要认识插件或业务字段。

页面声明 `declarative.backendConfig`，绑定 `settings.schema`、`settings.read`、`settings.save`。描述按游戏预设返回分组、布尔或数值控件、绑定路径与范围约束；读取和保存使用完整配置对象，隐藏字段不会因当前页面不可见而丢失。

通过 `tools/build-plugin-backends.ps1 -Backends player-tweaks` 构建后台，再通过现有打包脚本生成包。此版本用于从 0.2.0 更新，仍保持 `publish: false`，真实游戏效果尚待验收。

# 原生注入器

默认模式通过 CBT Hook 加载 Runtime，并按 `d3dx.ini` 的 `inject_dlls` 顺序处理附加 DLL。`--preload-runtime` 在挂起目标中先加载 SSMT Runtime，再加载附加 DLL 和可选 PluginHost，最后恢复目标主线程；这用于需要在图形初始化前建立完整链路的热注入场景，不向游戏目录放置代理 DLL。

`--set-env NAME=VALUE`、`--unset-env NAME` 可以重复使用。环境在提权后的 Run.exe 内设置，由本次创建的游戏继承，不修改 SSMT、系统或用户的环境；不能与外部目标的 `--inject-mode direct` 一起使用。Hosted ReShade 应显式传入其 DLL、配置路径及图形 Hook 开关，避免依赖 ShellExecute 跨提权继承环境。

终末地使用 `--inject-mode direct --wait-for-target --process-timeout 120`，目标由官方启动器创建。目标匹配完整 EXE 文件身份，Runtime 检测也匹配实际 DLL 文件身份；系统同名 DLL 不视为成功。失败不会结束外部创建的游戏。

以独立目录配置 `SSMT_INJECTOR_REQUIRE_ADMIN=OFF`、`SSMT_INJECTOR_BUILD_TESTS=ON` 后，构建夹具并执行 CTest。测试覆盖真实进程注入、额外 DLL 顺序、PluginHost、同名进程/DLL、失败处理和预加载目标的环境继承与删除。正式目标保留管理员 manifest。夹具通过不代表真实游戏的反作弊、图形或 NR 已验收。

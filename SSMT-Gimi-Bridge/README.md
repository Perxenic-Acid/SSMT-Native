# 原神 FSR2 Bridge 的 SSMT 适配

可选 DLSS5 插件的辅助组件，不是 Plugin ABI DLL。默认随 Native 构建生成，
由 `ssmt.dlss5.integration` 0.2.1 插件包携带，安装并按游戏启用后才准备注入。
`-DSSMT_BUILD_GIMI_BRIDGE=OFF` 可关闭此目标，普通注入器不依赖它。

固定上游 [1.2.3 源码 cbdf5c1](https://github.com/AizawaHikaru233/genshin_fsr_brigde/tree/cbdf5c11bfea8d4cb423866dabb96a755fb8ab0b)，
下载归档后验证 SHA-256，在构建目录应用 `per-instance-methods.patch`。
保留 FSR2 转译导出，不引入新版 FFX12 后端、安装器或其他游戏补丁。
GPL-3.0 及第三方声明随插件包分发；完整对应源码由固定上游源码、补丁及本目录组成。

## 源码归档与离线构建

不同构建目录共用 `native/.cache/downloads`，可用 CMake 参数
`SSMT_NATIVE_DOWNLOAD_CACHE` 指定其他缓存目录。配置时先校验共享归档，再尝试当前
构建目录及旧 `native/build` 中的同版本 ZIP；只有哈希正确的完整归档才会被复用。
每个构建目录仍单独解压和应用补丁，不共用已修改的上游源码。

无可用缓存时从固定上游下载，最多尝试三次，每次最多 120 秒、连续无数据最多 30 秒。
下载写入独立临时文件，通过 SHA-256 后才替换共享缓存；失败清理本次半包并停止配置，
不会继续解压。并行构建通过归档锁协调，无法取得锁时给出缓存占用提示。

可在 SSMT 主仓库根目录先配置离线 ZIP，再重试原构建入口（参数会保存在对应 CMake 缓存）：

```powershell
cmake -S native -B build/native-release -A x64 `
  -DSSMT_GIMI_BRIDGE_ARCHIVE="C:/Downloads/bridge-cbdf5c11bfea8d4cb423866dabb96a755fb8ab0b.zip"
```

离线 ZIP 也必须来自上述固定 commit，SHA-256 为
`996cd2ef72df58748d029d2dec525c486e11edc78b7761ae7a4ec7612aee6f8c`。
显式路径不存在或哈希不符时直接报错，不改用另一份归档或跳过校验。
删除该离线文件前可用 `-DSSMT_GIMI_BRIDGE_ARCHIVE=` 清除缓存中的路径。

下载逻辑的回归不访问外网、不加载图形 DLL，可在主仓库根目录运行：

```powershell
python native/SSMT-Gimi-Bridge/tests/test_archive_download.py
```

## 对象转发适配与图形验证

上游将第一个 COM 对象的方法保存在进程全局，导致 Runtime 包装设备的方法被
用于系统/显卡辅助设备。SSMT 改为按对象查找原始虚表；稳定调用使用线程缓存，
不增加每次渲染的锁、分配、文件读取或模块查询。最终 Release 清理该代对象的
登记，避免地址复用时沿用旧方法；影子虚表继续保留至进程退出，避免缓存 ABA。

```powershell
cmake -S . -B build-gimi-bridge -A x64 -DSSMT_GIMI_BRIDGE_BUILD_TESTS=ON `
  -DSSMT_GIMI_BRIDGE_TEST_RUNTIME='完整路径/d3d11.dll'
cmake --build build-gimi-bridge --config Release --target DeployGimiBridge gimi-bridge-interop-test
ctest --test-dir build-gimi-bridge/SSMT-Gimi-Bridge -C Release --output-on-failure
```

无 Runtime 参数时仅运行两个不同 COM 实现、复制虚表、地址复用和多线程 Context
转发测试。提供 Runtime 后另建真实 WARP 辅助设备，热加载 Runtime 创建包装设备，
交替创建 Buffer 和调用两个 Context；必须通过 Runtime 私有身份校验、退出码及
完成标记。测试不写游戏目录，也不验证 NR 画面。

回归的反例可用 `SSMT_GIMI_BRIDGE_TEST_SOURCE` 指向同版本未修复的
`Dx11FsrBridge.cpp`，只替换测试 EXE 的实现；发布 DLL 仍应用补丁。
旧实现的混合真实设备测试复现 Runtime `CreateBuffer` 的空指针异常，
因此不能只检查 DLL 是否加载或给 Runtime 添加空指针判断。

产物位于 `dist/<配置>/SSMT-Gimi-Bridge.dll`，版本 1.2.3.1。构建脚本将它及许可证
放进插件包并生成组件哈希。构建包不会自动安装或启用插件；本地更新通过插件市场
已有入口完成，已有的 NR 下载和游戏选择可以保留。

`gimi-fsr2-dispatch-test` 是另一个显式运行的实组件夹具，需要 NVIDIA 显卡，
参数依次为托管 ReShade、Bridge、OptiScaler 的绝对 DLL 路径，并传入准备计划中的
环境。它通过系统 D3D11 设备验证 FSR2 Create、两次 Dispatch、Destroy；主应用
`prepares_real_components_without_game_directory_writes` ignored 测试可通过
`SSMT_HOT_FSR2_FIXTURE` 调用它。错误的 `UseFsr2Dx11Inputs=auto` 可复现实机
OptiScaler `+0x1C3E57` 崩溃，修复配置走 DX11 输入并完成 DLSS Feature 创建。
该项不算 Runtime 共存或 NR 验收；混合设备测试继续单独确认 Runtime 私有身份。
没有 SwapChain 时只执行两次 Dispatch，避免等待 Present 才回收的环形缓冲。

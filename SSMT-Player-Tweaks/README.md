# SSMT Player Tweaks

## 原神原生缩放范围解锁

在 SSMT 插件页的镜头设置中开启“解锁原生缩放上下限”，保存后下次通过 SSMT
启动游戏生效。仅提供开关，默认关闭，没有倍率或距离范围配置。
滚轮输入、速度积分、原生距离换算与相机状态更新仍由游戏执行。
插件只跳过已验证的缩放钳制，不写入固定距离、倍率或无穷大。
开启后也会保留切入角色聚焦镜头前的手动缩放比例，跳过进入时的 `1.0` 重置。
临时镜头的距离计算仍由游戏处理，插件没有全局距离缓存或每帧恢复距离的逻辑。

同目录的 `SSMT-Player-Tweaks.ini` 使用现有镜头配置节：

```ini
[Camera]
CameraZoom=0
```

旧试验版的 `MaximumZoomRatio` 已移除；残留配置会被忽略，保存时不再生成该项。
开关关闭时不扫描或安装缩放 Hook。未启用本游戏的 Player Tweaks 插件时沿用普通启动路径。

当前解析器针对原神 **7.1 CN/Bilibili** 样本完成离线验证：
`YuanShen.exe` SHA-256：
`7F89938DA606C1281659607D464702CDDB9A09A7D4320A196630F60A811EC38E`。
通过现有 PatternScanner / ResolveSymbol 确认八个函数的唯一有效候选，
再检查十二个限制点及一个聚焦比例重置点的寄存器流、分支、原生 `1.0` 常量、
IFix 标志和输入/距离写回布局。
除输入系数外，还覆盖半径更新的两个 Clamp 调用、距离平滑中的差值限制和最终半径限制。
局部布局不符、候选不唯一或相关 IFix 分支已激活时只停用此功能，没有固定 RVA 回退。

沿用 MinHook，在限制块入口传递未钳制的标量值并跳回原生后续运算。
共享 SmoothDamp 只在已验证的相机半径更新调用点解除限制，其他调用者走原始 trampoline。
原生 min/max getter 与距离标尺不变；碰撞和距离平滑的后续计算继续执行。
所有 Hook 先创建为禁用状态，再批量启用；创建/启用失败时移除本功能已创建的 Hook。
跳转代码随进程常驻，热路径没有 C++ 回调、分配、锁、文件读取或日志。
保护计时器和速度衰减中的插值系数钳制仍由游戏处理。

实机尚需验证滚轮拉近/拉远、停止输入、碰撞、瞄准/过场/传送、场景切换和镜头功能组合。
弓箭重击结束后恢复超远距离的问题暂时搁置；聚焦比例保留已实现并通过局部测试，
完整重击流程尚未实机确认，不能视为该问题已解决。
离线结果不能保证所有镜头状态或其他客户端均无额外限制；运行中新增的 IFix 替换也未验证。

## 局部验证

从 native 仓库根目录构建：

```powershell
cmake -S . -B build
cmake --build build --config Release --target SSMT-Player-Tweaks CameraPolicyTests CameraZoomTests --parallel
```

运行 `build/SSMT-Player-Tweaks/Release/CameraPolicyTests.exe`，以及：

```text
CameraZoomTests.exe <camera_zoom_limits_research 证据目录> [上述哈希对应的 YuanShen.exe]
```

本工作区证据位于主仓库的 `tmp/task_7Oct/camera_zoom_limits_research`，
可用同级 `inspect_camera_zoom_limits.py` 从已核对样本重新提取。
测试覆盖每个限制点的实际 MinHook 跳转、范围内数值保持、正负越界值通过、实际半径写回、
共享平滑函数的调用者隔离、调用关系变化拒绝安装、
聚焦进入/保持/退出时的比例保持、临时半径覆盖、多个相机对象之间的独立性、
移除后恢复限制、布局/常量破坏、IFix 激活、候选歧义、安装冲突回滚和镜头功能组合。
测试中的通用 SSE 限制块在独立 ABI 测试函数中运行；完整游戏函数只映射并扫描，绝不调用。
传入完整样本前应核对哈希；研究脚本及游戏证据不随插件分发。

# Liquid Gravity Probe

此目录由 SSMT Liquid Gravity Probe 插件随包提供，通过 `server.py` 启动本地编辑器后端。需要 Python 3.12 或更高版本，不需要 pip 第三方包，也不需要单独的编辑器 EXE。

服务器只监听本机回环地址。每次启动都会使用随机端口和实例 token，关闭 SSMT 编辑器后由宿主进程停止。

## 数据目录

插件的配置、运行文件、模型注册、日志和清理状态统一写入：

`<EFMI>/Mods/SSMT_LiquidGravityProbe`

插件不依赖 `LiquidGravityProbe-*` 文件夹，也不创建历史版本所需的 `LiquidGravityProbe_Profiles` 或 `output/backups`。修改 Mod 时先写入内存事务；失败时只在当前进程内回滚，不保留持久化备份副本。

## 编辑器功能

1. 从 EFMI `Mods` 中选择包含可读 VB/IB 网格的 Mod。
2. 在 Three.js 预览中旋转、平移、缩放并点选表面。
3. 配置最多 4 个喷口、附着位置、喷射方向、粒子颜色、粒子池和按键开关。
4. 保存后回到游戏按 F10 重新加载。
5. “永久删除探针添加的内容”只移除探针写入的钩子、配置和运行数据，不删除原 Mod、模型、贴图或原生网格诊断文件。

## 兼容范围

- 支持标准 EFMI 的 float3/float4 位置缓冲、R16/R32 索引、Mod_yujin Position 缓冲和长椅模块的静态 Position 缓冲。
- 预览使用原始姿态显示单色网格，不重现游戏动画、GPU ShapeKey、服装物理、着色器或贴图效果。
- 同一文件中的互斥服装分支会一起读取，可用部件显示开关查看。
- 同一角色不同 draw 需要分别配置附着点。
- 游戏内最终效果需要在目标服装和动画状态下确认。

插件源码和开发说明见上一级目录的 `README.md`。

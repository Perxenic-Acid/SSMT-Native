# Bullet CPU 刚体核心

上游：<https://github.com/bulletphysics/bullet3>
版本：`3.25`，commit `2c204c49e56ed15ec5fcfa71d199ab6d6570b3f5`。
授权：zlib，完整条款保留在 `LICENSE.txt` 与源码头部。

仅保留 `src/LinearMath`、`src/BulletCollision`、`src/BulletDynamics`、两个公共头部和许可证，
共 394 个未修改上游文件、约 4 MiB。`SOURCE.json` 记录每个上游文件的 SHA-256。
`SSMT.cmake` 是本项目的构建适配，直接静态链接进可选 Poser 插件，不要求用户另装物理 DLL。
构建不访问网络；没有启用 Python、GPU、演示、软体或图形渲染依赖。

MMD 刚体 / 骨骼变换和关节建图流程参考 Saba MMDPhysics 与 Three.js r170 MMDPhysics；
本项目没有复制这两个项目的实现源码。自动骨链 profile 由静态 Avatar / Body 推导，
不宣称恢复了缺失的 PMX 刚体、碰撞分组和作者参数。

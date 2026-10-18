# SSMT 适配 Bridge

上游：<https://github.com/AizawaHikaru233/genshin_fsr_brigde>，
固定提交 `cbdf5c11bfea8d4cb423866dabb96a755fb8ab0b`（1.2.3），GPL-3.0。
原作者版权声明保留在源码及 LICENSE.txt 中。

SSMT 修改：按 COM 对象转发原始方法，处理对象地址复用；版本标记为 1.2.3.1。
保留原有 FSR2 转译导出，不切换新版 FFX12 后端。

构建及完整补丁位于 SSMT Native 的 `SSMT-Gimi-Bridge`：
<https://github.com/Perxenic-Acid/SSMT-Native/tree/main/SSMT-Gimi-Bridge>。
构建下载固定归档并核对 SHA-256，应用可审阅补丁后再编译。
该组件仅随 DLSS5 插件分发，安装并按游戏启用后才会准备热注入。

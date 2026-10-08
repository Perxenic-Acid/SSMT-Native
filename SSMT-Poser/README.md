# SSMT Poser：原神 Rig 与动态 VMD 动作实验

2026-10-07 的实机测试已在 native metadata resolver 基础上恢复真实 System.RuntimeType，
在核验过的 Unity 主线程调用非泛型 FindObjectsOfType(Type)，取得 GameManager 的
Transform 实例，读取 GameObject.name，并交叉验证 childCount getter。
后续场景采集已取得真实 SMR/Animator、骨名和 parent 层级。本轮增加 Actor ancestry
匹配，以及默认不触发的单骨旋转研究；ScriptRunBehaviourLateUpdate 返回后的两轮
左上臂变化 / 恢复已有用户视觉确认，历史 Head 4°视觉结果仍未确认。
动态 VMD FK 加载 / 播放 / 停止代码已通过本地验证，实机反馈指出足 IK 缺失。
新增自动只读采集和两骨足 IK 预览，仍需实机校准，不宣称完整 MMD retarget。

插件只通过现有 PluginHost ABI v2 工作，不依赖 Player Tweaks，不修改 3DMigoto，
没有前端页面或渲染回调。只有插件已安装且为 GIMI 启用时进入现有启动计划。
DLL 还检查进程名；在非原神进程中保持不活动。普通无插件路径不依赖此 DLL。

## Native backend

`GenshinNativeRuntime` 独立包含当前原神的注册/cache/metadata 解码；
上层得到 `NativeClass` / `NativeMethod` 的 opaque 借用地址。
`FindClass(namespace, name)` 和 `FindMethod(class, name, parameterCount)` 拒绝歧义匹配；
这些接口属于只读解析能力。`LiveUnityProbe` 仅提供少量经过 callee/consumer 核验的
typed call，不提供通用 RuntimeInvoke 或完整 MethodInfo / invoker 布局。

当前只支持两个 SHA-256 均匹配的 GI 7.1 CN 样本：

```text
YuanShen.exe
7f89938da606c1281659607d464702cddb9a09a7d4320a196630f60a811ec38e
global-metadata.dat
469eccd43aa48fe7a2df4e1caf66fa1268a17f5a4a03fa209e1427e467cb0161
```

在已加载 EXE 的可执行段扫描签名，再通过 RIP-relative load/store 与 rel32 call/jmp
恢复注册结构及 class/method cache。注册签名本身有多个候选，必须与独立 method
消费者指向同一个 MHY header；语义筛选后要求唯一。
没有固定 RVA fallback，也不把历史 offset 当作其他版本的通用布局。

本 profile 的字段来自当前 constructor / consumer 反汇编，并在实机交叉检查：
class 的 metadata record、类型名和方法数量必须匹配；
method descriptor 的 class、metadata ID、参数数量和代码入口必须匹配。
泛型定义允许合法的 NULL code entry，诊断明确标记不可调用。

标准导出 backend 的 metadata 枚举基础设施仍保留并测试；
当前原神执行链直接进入 native backend，不重复 Phase 0 export discovery。
未知样本或 native 校验失败时停止，不能降级到未经核验的固定地址。

## 构建与开发包

在 native 根目录运行：

```powershell
cmake -S SSMT-Poser -B build-poser -A x64
cmake --build build-poser --config Release --target SSMT-Poser PoserProbeTests --parallel
ctest --test-dir build-poser -C Release --output-on-failure
./tools/pack-official-plugins.ps1 -ReleaseTag v-test -DevelopmentPluginId ssmt.poser.genshin-probe -OutputDirectory ./dist/poser-dev-packages
```

DLL 默认复制到 `dist/Release/SSMT-Poser.dll`。可以用
`-DSSMT_POSER_ARTIFACT_DIR=<新绝对目录>` 指定独立输出，打包时对应使用
`-NativeArtifactDirectory`。输出目录必须为空。测试启动先复制 DLL 到隔离目录，
避免游戏占用构建产物。

包 ID 保持 `ssmt.poser.genshin-probe` / 0.1.0。官方包只能通过 Native Release
来源、目录、大小与哈希校验安装。开发包使用 `dev.ssmt.poser.genshin-probe`，
走现有本地第三方包确认入口。DLL 不会自动安装到用户插件库或修改启用设置。
主 CMake 的 `SSMT_BUILD_POSER_PROBE` 默认关闭，不构建 SSMT 发行包。

## 实机采集

通过 SSMT 安装并为原神启用后，完全退出游戏，再通过 SSMT 启动。
报告位置：

```text
%LOCALAPPDATA%/SSMT/Diagnostics/SSMT-Poser/<PID>-<FILETIME>/genshin_poser_runtime.txt
```

worker 初始化阶段验证文件哈希并恢复签名；之后每 5 秒观察 cache，最多 24 次。
不会调用 materializer 强制填充缓存。读取失败、歧义、身份不一致或取消时停止；
shutdown 等待 worker 退出后才允许卸载 DLL。

metadata 就绪后另行恢复唯一的 RuntimeType factory、thread_current、thread_attach
与 GC handle 接口，并交叉核验缓存/TLS/handle table。临时 WH_GETMESSAGE hook
每次只派发有界任务：窗口线程 ID 必须等于游戏内部 Unity main-thread 记录，
callback 内再次验证线程 ID 及非空 IL2CPP TLS。借用既有附着线程，不调用 attach/detach。
独立 worker 的 attach/detach 调用路线尚未实现或实测。
反射类型及数组/Transform/GameObject 由强 GC handle 保护，读取完成即释放。
SEH 边界记录错误并停止；临时 hook 卸载后等待在途 callback 结束。卸钩失败时
保留 DLL 代码映射并报告失败，避免悬空 callback。

## 场景组件与骨名采集

当前探针在 metadata/新 getter 校验完成后等待用户进入可控角色场景。
开启 NumLock，进入后在游戏前台按小键盘 0 开始只读采集，也可由测试助手在本次 diagnostics 目录写入
`scene_ready.flag`。场景确认最多等待 15 分钟，取消/游戏退出会停止 worker。
确认后每 5 秒重试 Animator/SkinnedMeshRenderer 枚举，最多 120 次，每次临时安装并移除
消息 hook；只读快照不跨 callback 持有 managed 引用。

读取 GameObject.name、Transform、Animator.isHuman、SMR.rootBone、SMR.bones.length、
sharedMesh.name。每个目标最多检查前 256 个精确类型实例；Animator 跳过 Transform
不符合当前精确类型支持范围的 UI 候选；SMR 优先骨骼最多且可完整导出的候选。
这不等同于确认该对象属于当前可控角色。

SMR bones 数组受强 GC handle 保护，数量必须与已核验 native 容器一致。
最多完整读取 512 个 slot，保持 null slot 的索引，验证非空项的 class/native 指针，
通过只读 Object.get_name 复制骨名。worker 将快照导出到同目录的
`genshin_poser_bones.txt`，含骨名、managed Transform、runtime class 和 native Transform 地址。
小键盘 0 快照不调用 setter，不冻结角色或禁用动画。

## Rig、Actor 与单骨实验

`RigProbe` 追踪每个 skin Transform 及最多 32 层外部 parent，检查 getter 返回与
native parent 字段一致，并用 GetChild 反向核验所选 skin 边。Neck 等无 skin 权重
的父节点仍进入图。输出 `genshin_poser_rig.txt` 和 `genshin_poser_rig_tree.txt`。

最多枚举 512 个 Animator/SMR 候选，记录名称、活动状态和 ancestry。仅当同一
Animator Transform 同时是 Body 与 Head 的祖先时，建立 Actor candidate；多个
renderer 按根与 Head 分组到 Actor，导出 `genshin_poser_actors.txt`。
精确类型不支持的 UI/祖先节点和已销毁候选会跳过；不可读、cycle 或过深路径停止。
骨数量最大策略仅用于诊断 rig 选择，不单独决定玩家身份。
已配置 Actor 名称时，先按真实 parent 链、活动状态及 AvatarRoot/EntityRoot 分支
选择该 Actor 的 renderer，再建立 rig；不会从其他 NPC 的最大骨数候选进入写入。

`BoneWriteProbe` 默认不武装。测试助手需先把已由只读快照识别的完整 Actor 内部名
写入本次诊断目录 `target_actor_name.txt`；场景快照必须满足核心骨链、Head→Neck、
Body/Head 共用 Animator、活动状态、唯一活动 AvatarRoot/EntityRoot 分支及目标名匹配。
它保留 Head、Actor 根、SMR、Animator 的 4 个强 GC handle，等前台小键盘 4 或
`write_ready.flag`；等待最多 5 分钟，取消/超时会在主线程释放 target roots。

首次调用已按当前样本反汇编恢复 Quaternion ABI：getter 使用 RCX=sret、RDX=this；
setter 使用 RCX=this、RDX=16 字节 Quaternion 缓冲区。每次读写核验方法描述、
对象 class/native 指针、GC target、共同祖先、活动状态、返回缓冲区 canary、四元数
有限值/范数和 getter/native 数据一致性。仅保存并修改 Bip001 Head 的 localRotation：
Head 实验绕父坐标系 Y 轴 4 度，观察单次写入 250 ms，然后最多 30 次、间隔 33 ms 重写固定 q1。
不累积旋转；结束、错误或取消时尽量恢复 q0，并立即回读验证，再释放 target roots。
identity 不一致时终止，不能向未经核验或已销毁的对象恢复写入。

写入实验沿用临时窗口消息 hook，不声称已找到 Animator 之后的 late update 时序。
日志区分读写回读与恢复成功、下一 callback 的覆盖情况和用户实际画面观察。
2026-10-08 用户要求更清晰的视觉对照后，新增可选 `arm_test.flag` 实验：
仍只写一根 `Bip001 L UpperArm`，核验其 parent 为 `Bip001 L Clavicle` 且属于同一 Actor。
小键盘 5 触发围绕父坐标系 Z 轴 60 度的两次短时写入；每次独立保存 q0、固定 q1 重写
30 次、间隔 50 ms，随后恢复。第一次恢复后保留 target roots，等待 1 秒再进行第二次；
最终恢复后全部释放。没有该 flag 时保持原 Head 实验；没有目标名时两种写入都不武装。
日志区分 setter/native 回读、每次恢复与视觉观察，不能把自然动画当作画面写入成功。
单骨模式只有 localRotation setter；没有 Animator disable、冻结或 UI。

## 动态 VMD 旋转预览

用户要求动态动作后新增独立的 `motion_mode.flag` 模式。开启 NumLock，小键盘 0 确认
场景并绑定唯一活动 AvatarRoot 下配置的 Actor；小键盘 1 从 diagnostics 目录的 UTF-8
`motion_path.txt` 读取路径并加载 / 重载 VMD 从头播放；小键盘 2 停止并恢复播放前的旋转；
小键盘 3 结束会话并释放 hook / GC roots。主键盘数字和 F 功能键不触发探针命令。
路径支持中文与空格，修改路径后无需重启游戏或重新加载 DLL。用户手动控制播放和停止；
自动化仅负责只读场景 / rig 准备，不由 agent 自动创建播放、停止标记。独立测试工具可使用同目录
`motion_play.flag`、`motion_stop.flag`、`motion_exit.flag`；这些标记只在已武装的动作会话消费。
存在 `auto_scene.flag` 且配置了动作模式和目标 Actor 名时，启动后自动执行只读 discovery，
最多每 5 秒重试、120 次。只有唯一活动 AvatarRoot 下的目标 rig 通过校验才武装，
不再要求采集按键或向 agent 回复场景确认；SEH / 在途派发失败仍停止，不能无限重试。
自动采集本身不启动 setter；手动小键盘 0 路径仍保留。

VMD 0002 解析检查文件边界、计数、帧号、有限数与有效 Quaternion；CP932 骨名转换为
Unicode，排序 / 去重后按 30 FPS 采样贝塞尔时间曲线和 Quaternion Slerp。
相机 / 表情文件可解析，但没有骨骼轨道时拒绝开始角色播放。
采用标准 MMD 主体 / 手指 FK 层级与目标 Mesh 的静态 bind reference；这是动作预览，
默认 FK 不求解足 IK；尚未实现完整 PMX bind pose / 附加轴转换、root 位移、表情、物理或相机播放。

播放前只读捕获 `Mesh.bindposes`，核验 descriptor、wrapper / native consumer 的 Matrix4x4
SZARRAY 返回与 64 字节复制布局，并与 native bind 数据逐项比较。只接受有限、可逆、
无 shear、正 scale 的矩阵；skin 索引必须完整匹配，Body 模型空间必须与所选 Actor 对齐。
矩阵按值保存，临时 Mesh / 数组 GC roots 即时释放，失败时拒绝播放，禁止回退到当前待机动作。
旋转基准与恢复快照分开：VMD 绝对采样作用于静态绑定旋转，现场旋转只用于停止恢复。
没有 VMD 轨道的蒙皮骨也覆盖绑定旋转并跟随已控制父骨；未蒙皮桥接骨的真实父旋转用于
计算 localRotation，以抵消其动画旋转。仍未覆盖骨骼 localPosition / scale 或禁用全部姿态系统，
因此不能声称已完全屏蔽游戏动画、物理和叠加效果。完整 A-pose 隔离与 PMX 轴校准继续待办。
模型的 bind pose 是否为 A-pose 必须从实际数据核验，不能由名称或单位 Quaternion 假定。

诊断目录 `foot_ik_scale.txt` 可显式配置正数、最大为 1 的“游戏单位 / VMD 位移单位”，
武装当前实验两骨 IK：按各轴贝塞尔曲线采样左右足 IK 位置，以静态 bind 脚位置为锚点，
实读目标 Thigh→Calf→Foot 的世界坐标和长度，解析求解膝 / 脚目标，写入大腿、
小腿、脚的 localRotation 并逐次回读。腿长度比例从实际腿长与静态绑定骨长对照；
膝盖方向采用静态绑定弯曲面；退化时
采用 Actor 局部 +Z。超出骨长范围的目标投影到可达范围，不拉伸骨骼。
验证实际脚坐标与投影目标的残差及腿长稳定性，失效即停止并恢复。
新位置 / 世界旋转 getter 均经当前样本 wrapper / native consumer ABI 校验，局部位置
与原生 TRS 字段对照，输出缓冲区带 canary。没有位置 setter 或 Actor root 位移写入。

该 IK 是带显式位移比例的预览：VMD 不包含源 PMX 骨长和 rest pose；没有完整
PMX grant / 足 D 骨修正、IK 开关轨道、脚趾 IK 或 root / center 位移，不能宣称还原源舞蹈。
报告分开记录 FK / IK quaternion 写入、两腿长度、求解次数、不可达目标数和最大脚位置残差。

写入在已核验的 ScriptRunBehaviourLateUpdate 返回后执行，目标与桥接父骨持有强 root，
每帧检查对象 / native / parent 身份并回读 localRotation；只写选定 Actor 的旋转。
初始化与文件读取在 worker；高频回调使用预建固定容量表，无分配、文件 IO 或日志。
失败 / 停止 / 文件重载先停止写入并恢复 q0；结束会话等待 hook 卸载并释放 roots。
输出有限样本及完整写入 / 恢复计数，视觉成功仍须由实际游戏画面确认。

结果：

- `LIVE_UNITY_OBJECT_CONFIRMED`：至少一个精确目标类型实例、native 对象、Transform、
  GameObject、名称、getter/native 字段相等及 GC/hook 清理已核验（第三阶段历史报告）。
- `SCENE_COMPONENT_CONFIRMED`：场景确认后，至少一个 Animator/SMR 的只读属性已核验；
  取得 SMR.bones 时会导出骨名快照。
- `PARTIAL_SCENE_PROBE`：场景确认或 typed call 某个校验未完成；查看 blocker 或 step/SEH/指针。
- `NATIVE_RESOLVER_NOT_INITIALIZED`：不支持的样本、签名或初始化校验未通过。
- `NATIVE_RESOLVER_STOPPED`：后续读取/身份校验失败或取消。
- `NATIVE_RUNTIME_CACHE_NOT_READY`：有界观察结束后 class cache 尚未全部就绪。
- metadata snapshot 内的 `invoked=NO` 仅表示该 snapshot 不调用游戏；
  后续 `[LIVE]` 段记录实际调用结果。空代码入口仍显示 `NULL_ENTRY_NOT_CALLABLE`。

worker 负责 discovery/报告，不调用 UnityEngine；Present 不参与派发。
场景确认后向已就绪窗口派发快照；窗口或组件尚未就绪时按上述次数有界重试。
没有持续每帧扫描或 IO；写入实验以强 GC handle 持有目标至恢复/取消完成。
报告中的裸地址只代表历史观察，不能跨快照直接复用。

## 验证范围

原生方法描述已经取得，但未恢复完整官方 MethodInfo / invoker / rgctx / exception ABI。
非泛型 FindObjectsOfType 使用真实 RuntimeType；其 descriptor 可以引用注册表，也可以
引用 class 中的副本，按当前样本的 16 字节类型内容核验，不能要求地址相等。
managed array 的 +18 长度与 +20 引用区来自当前 allocator/枚举器；还核验 SZARRAY
类型及 element descriptor，禁止把常见 IL2CPP 布局套用到未知样本。

阶段 3 证据记录启动阶段 Transform；场景实测结果和骨名快照见本机任务报告。
当前提供有界 parent 图和受控单骨实验；具体成功范围须以本机实测日志及用户视觉确认为准。
头部微小旋转的历史反馈已由用户更正为视觉未确认，不能凭即时 setter 回读宣布操控成功。
用户要求的明显上臂对照实验只写左 UpperArm 的 localRotation，60°，两轮修改/恢复。
当前实验在双哈希 profile 下通过完整指令签名、`ScriptRunBehaviourLateUpdate` marker
和实际注册槽恢复无参 void 回调；临时 hook 先执行原函数，再在已核验的 Unity 主线程
对强 GC handle 持有的单骨写固定 q1。每轮有 2.5 秒写入上限，正常透传路径无 Unity 调用、
分配或 IO；采样数组最多 128 项，完整命中数另计。卸载等待在途调用，失败时 pin 模块。
这是短时时序实验，是否留到渲染画面仍须实机及用户观察，不提供长期 pose ownership。
MinHook 复用工作区的已有源码并静态链接；包内 `MinHook-LICENSE.txt` 保留原始许可。
`tmp/poser-probe/CODEX_RESULT.md`、运行日志、JSON 和有界反汇编记录本机研究证据；
研究脚本中的当前样本 RVA 不属于插件部署 resolver。

## 参考来源

最初探针计划参考 honxi1/Endfield-Poser 和 OedoSoldier/Endfield-Poser。
当前探针与 native backend 为独立实现，复用本工作区同样本的 metadata 解码研究，
没有迁移这两个外部仓库的实现或依赖。后续若直接迁移源码，须保留来源与许可证。

# SSMT Poser：原神 Rig 与动态 VMD 动作实验

2026-10-07 的实机测试已在 native metadata resolver 基础上恢复真实 System.RuntimeType，
在核验过的 Unity 主线程调用非泛型 FindObjectsOfType(Type)，取得 GameManager 的
Transform 实例，读取 GameObject.name，并交叉验证 childCount getter。
后续场景采集已取得真实 SMR/Animator、骨名和 parent 层级。本轮增加 Actor ancestry
匹配，以及默认不触发的单骨旋转研究；ScriptRunBehaviourLateUpdate 返回后的两轮
左上臂变化 / 恢复已有用户视觉确认，历史 Head 4°视觉结果仍未确认。
动态 VMD FK 加载 / 播放 / 停止代码已通过本地验证，实机反馈指出足 IK 缺失。
保留自动只读采集。当前显式足 IK 开关启用基于静态参考、局部膝轴限制和独立脚尖控制器的
标准 MMD 腿链；旧自由 pole 求解器不再进入播放写入路径，第 72 次测试的用户视觉反馈已通过。
当前增加所选骨架次级骨的实时物理与动作预烘焙；游戏中的物理画面及接管时序尚待验证。

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
cmake --build build-poser --config Release --target SSMT-Poser PoserPluginVersionTests PoserProbeTests PoserUiTests PoserLegIKTests PoserPhysicsTests PoserPhysicsUiTests --parallel
ctest --test-dir build-poser -C Release --output-on-failure
./tools/pack-official-plugins.ps1 -ReleaseTag v-test -DevelopmentPluginId ssmt.poser.genshin-probe -OutputDirectory ./dist/poser-dev-packages
```

DLL 默认复制到 `dist/Release/SSMT-Poser.dll`。可以用
`-DSSMT_POSER_ARTIFACT_DIR=<新绝对目录>` 指定独立输出，打包时对应使用
`-NativeArtifactDirectory`。输出目录必须为空。测试启动先复制 DLL 到隔离目录，
避免游戏占用构建产物。

包 ID 保持 `ssmt.poser.genshin-probe`，当前包版本为 `0.1.1`。DLL 的 ABI 查询版本自动读取
最高获准发布的包清单；更新时新增版本目录，不能只修改 Native Release 标签。
`0.1.0` 不再重打或进入新的发布目录。
官方包只能通过 Native Release
来源、目录、大小与哈希校验安装。开发包使用 `dev.ssmt.poser.genshin-probe`，
走现有本地第三方包确认入口。DLL 不会自动安装到用户插件库或修改启用设置。
主 CMake 的 `SSMT_BUILD_POSER_PROBE` 默认关闭，不构建 SSMT 发行包。

正式发布前，CI 将 `release-status.json` 中的 `sourcePaths` 与 GitHub 最新正式 Release
标签比较。源码、共享 MinHook 或包清单改变却未升级包版本时直接失败；首次安装、市场刷新
及主应用构建都不会把本地 DLL 自动覆盖到玩家插件目录。发布后在插件市场刷新，已安装
`0.1.0` 的玩家会看到 `0.1.1` 更新按钮；关闭游戏后更新，下一次启动使用新包。
`Validation/PluginVersionTests.cpp` 在独立进程验证实际 DLL 的 ABI 查询版本与包版本一致，
不启动游戏、不调用插件初始化；即使可选本机测试源码缺失，CI 仍构建并执行此项。

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
单骨模式只有 localRotation setter；没有 Animator disable 或冻结。控制面板用于独立的动态动作模式。

## 骨架与动作控制面板

启用插件并通过 SSMT 启动原神后，自动显示独立的 Windows 控制面板。它保留独立输入队列，
在游戏处于前台时置顶叠加，切到其他应用或最小化游戏时隐藏；关闭按钮只隐藏面板，
不会播放、停止或释放骨架。开启 NumLock，小键盘 `.` 显示 / 隐藏。界面使用系统控件、
GDI 与 Shell 文件选择器，不安装渲染 hook，不新增 Present callback 或修改 Plugin ABI。
窗口 / 无边框模式可使用这种叠加窗口；独占全屏下的显示效果尚未实机验证。

“光标穿透”默认关闭：面板可见且游戏处于前台时释放光标锁定、阻止游戏回中，并屏蔽游戏鼠标输入，
面板和文件选择器仍可正常点击。开启后面板穿透，鼠标交还游戏；`Ctrl + 小键盘 .` 随时切换穿透，
避免穿透后无法点击按钮。隐藏面板、最小化、切出游戏和卸载时停止接管；切到其他应用不会恢复游戏的锁定区域。
输入处理参考 [ReShade 的 Win32 输入实现](https://github.com/crosire/reshade/blob/main/source/input_windows.cpp)，
独立实现系统 API hooks，不修改游戏 WndProc 或 Unity 光标字段。鼠标 raw input、消息与按键查询被隔离，
键盘热键保留；UI 线程拥有真实光标位置。接管时在游戏输入线程释放原有鼠标捕获，阻止游戏重新捕获，
并保存 / 恢复游戏的光标显隐请求。面板不使用跨线程的游戏窗口 owner，避免 Windows 隐式合并输入队列。
输入 hooks 与动画 / IK 共享本 DLL 的 MinHook，最后才清理运行库。

操作顺序：

1. 自动只读采集场景；选择列表中“可绑定”的当前活动角色，点击“绑定所选骨架”。
2. 点击“选择…”或拖入 / 填写 VMD 路径，点击“载入 / 重载”。载入不会自动播放。
3. 点击“播放”；点击“停止并恢复”恢复播放前旋转。显示动作轨道数、时长及播放进度。
4. 停止后可“载出”动作、选择其他文件，或“释放骨架”并重新绑定；这些操作无需重启 DLL。

“次级骨物理”提供保留游戏物理、实时物理、物理预烘焙三种模式，默认为保留游戏物理。
实时 / 预烘焙模式可逐项选择候选骨，选择柔和、标准或偏硬参数。预烘焙须先载入动作，
再点击“生成 / 读取”；完成后才允许播放。生成时“停止并恢复”或游戏前台小键盘 2 可取消，
取消后保持骨架与动作绑定。切换物理设置和生成期间不能播放、换骨架或换动作。

只允许绑定唯一活动 AvatarRoot / EntityRoot 分支的角色；其他 Actor / NPC 可出现在只读列表，
非活动角色不能绑定。选择携带快照 generation，worker 拒绝旧快照、同名歧义及只读候选；
随后在游戏主线程重新枚举并检查真实身份，界面不持有 Unity 裸指针。
自动扫描每轮最多 120 次、间隔 5 秒；超时后等待手动“刷新”，不无限扫描。
有既有 `target_actor_name.txt` 时保留自动绑定已配置目标的开发流程。

播放期间禁用换骨架、刷新和文件选择 / 载入 / 载出，停止始终可用。
停止命令优先并取消尚未执行的播放请求。关闭面板不会影响正在播放的动作；
插件 shutdown 先停止 worker、恢复并释放游戏资源，再等待 UI 线程及模态文件选择器退出。
文件选择器使用 `IFileOpenDialog`，不改变游戏进程的工作目录。
界面和 worker 通过有界命令队列及按值快照通信；LateUpdate 写入只发布原子进度数值，
不访问 UI mutex、窗口、文件或字符串。

`PoserUiTests` 覆盖过期 / 歧义 / NPC 选择、中文路径、控件状态、停止优先及窗口重建。
以下独立演示不连接游戏，可检查真实控件、保存截图，并测试文件选择器取消与卸载：

```powershell
./build-poser/Release/PoserUiTests.exe --preview C:/已有目录/poser-controls.png
```

真实游戏中的 GUI 绑定 / 播放 / 恢复流程仍需实机验证；演示数据不能当作游戏采集结果。
现代文件对话框接口参考 [Microsoft Common Item Dialog](https://learn.microsoft.com/windows/win32/shell/common-file-dialog)。

## 动态 VMD 旋转预览

GUI 启用时自动进入动态动作模式及只读采集，无需采集按键。旧开发流程也可使用
独立的 `motion_mode.flag` 模式。开启 NumLock，小键盘 0 确认
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
默认 FK 不求解足 IK。当前 `全ての親 -> センター -> グルーブ` 位移按 `MODEL_Y_180`
组合后叠加到静态 Avatar 的 `Bip001.localPosition`，并在停止时交还原生位置；比例仍需按实际
PMX/实机轴测校准。未被 VMD 接管的蒙皮骨不再被默认静态写回，以便游戏原生二次运动继续运行。
完整 PMX bind pose / 附加轴转换、PMX 复合与骨骼表情、足 D、Grant 和相机播放仍未实现；
VMD 文件中的 IK 开关轨道已解析；标准腿链与脚尖 IK 通过下述显式开关启用，默认只播放 FK。

播放前只读捕获 `Mesh.bindposes`，核验 descriptor、wrapper / native consumer 的 Matrix4x4
SZARRAY 返回与 64 字节复制布局，并与 native bind 数据逐项比较。只接受有限、可逆、
无 shear、正 scale 的矩阵；skin 索引必须完整匹配，Body 模型空间必须与所选 Actor 对齐。
矩阵按值保存，临时 Mesh / 数组 GC roots 即时释放，失败时拒绝播放，禁止回退到当前待机动作。
普通播放和 FK 对照均捕获 Avatar 静态 `m_DefaultPose`，核验名称与路径 CRC、真实父链和读取稳定性，
并与全部 Body bindposes 交叉比较。通过后补齐未蒙皮的 Bip001/Pelvis/桥接骨模型参考，保存受控骨链
的静态 localPosition；获取失败时拒绝播放。VMD 绝对采样作用于静态参考，受控链同时覆盖局部位置
与旋转。原生姿态只用于停止交还，交还采样排除仍留在 Transform 上的 Poser 上一帧写入值。
未接管的蒙皮骨由游戏自身的动画/物理系统决定；选用次级骨物理时额外接管所选骨及所需静态父链。
不覆盖 scale，也不禁用整个姿态系统。
因此游戏次级运动、叠加效果与源 PMX 轴校准仍须独立验证。
模型的 bind pose 是否为 A-pose 必须从实际数据核验，不能由名称或单位 Quaternion 假定。

VMD 第 0 帧可以包含非单位旋转与非零位移，它代表开场动作，不自动等于源模型的静态姿态。
加载报告输出 `[VMD_START_POSE]` 和映射骨的 `[VMD_START_BONE]`，记录源模型名、首帧旋转 / 位置
及首关键帧编号。保留这些动作值，不自动用首帧求逆或减掉位置。
源 PMX 的静止姿态 / 骨轴与目标 Mesh 的 A-pose 是两套参考；当前标准 MMD 层级未取得源 PMX
reference，不能由首帧反推出模型基准。需要独立核验源 / 目标参考转换和 IK / center 位移关系。

诊断目录同时存在 `foot_ik_scale.txt` 正比例和显式 `foot_ik_write.flag` 时才请求腿 IK 写入。身体和腿目标始终以静态 Mesh/Avatar 参考为动作零点，原生趴伏、待机或跳跃姿势仅用于停止交还；实时肢段长度只用于适配场景缩放及写入校验。普通播放也使用静态 `Bip001` 位置，既有 `+PelvisTwist CF A01` 下半身映射不再仅限于 FK 对照模式。

启用足 IK 时，`foot_ik_scale.txt` 同时标定身体与足控制器的统一位移单位，不再让 Center/Groove 固定为 0.1、只改变脚轨迹。身体先按静态 FK 与 VMD 平移重建，再根据双腿静态骨长和本帧足目标，求共同身体平移的最近可达位置。适配只改变 `Bip001` 的本帧位置，保留足目标、静态局部骨长与膝轴；关闭的足 IK 不参与约束，下一帧重新计算，停止沿用原生位置交还。实时与烘焙共享同一算法，物理驱动版本为 5，旧驱动的烘焙缓存不再复用。

`[ROOT_REACH] solver=RETARGET_REACH_ROOT_V1` 记录适配次数、无可达交集次数与最大位移；`[FOOT_IK] max_goal_residual` 记录到本帧原始混合目标的误差，区别于 `max_residual` 对求解器投影结果的写入校验。这是不同骨长角色的可达适配，还未求解重心或支撑平衡，不能保证源动作视觉姿态精确一致。

当前采用明确声明的标准 MMD 腿链：身体分支包含 Center/Groove/下半身，控制器分支为 `全ての親 -> 足IK親 -> 足IK -> つま先IK`。VMD 位置按静态参考上的局部偏移处理，保留首帧动作，不减去第 0 帧；Center/Groove 不重复加入足目标。膝盖在静态几何校准的局部单轴上屈曲，大腿保留 FK twist 后瞄准目标。足 IK 控制器的旋转用于合成脚尖目标，不直接覆盖脚骨世界朝向；脚尖 IK 单独消费自身开关并求解脚骨。有限残差超限时恢复本帧 FK，身份或 setter 失败仍停止并进入恢复路径。

仍以游戏 `Miscs.CalcCurrentGroundHeight`（metadata ID `702885`）记录地面诊断，不把 VMD 目标夹到地面；查询无有效高度时记录失败，继续 MMD 求解，不宣称鞋底贴地已校准。当前 VMD 的两条足 IK 親轨道只有零偏移、单位旋转，满足此 profile；其他动作若足 IK 親发生旋转，由于缺少源 PMX 静态枢轴，会明确拒绝 IK 写入。VMD 不包含源骨长、层级、轴限制或 Grant；非标准 PMX、足 D 和附加变换仍需源模型证据。共同身体适配仍无法满足的目标继续按腿长投影，并保留无解次数与原始目标误差，不能以很小的求解器残差代替真实游戏视觉验收。目标 Animator 的 FBIK 链盘点为空，因此本阶段使用受控 Transform 解算。

可重复的 IK 语义回归位于 `Validation/LegIKTests.cpp`，随源码构建为 `PoserLegIKTests`。CTest 覆盖标准直腿、连续转身、局部膝轴、首帧偏移、控制器父子旋转与脚尖目标；传入 VMD 路径可遍历真实动作全部整数帧。这些离线检查不包含 Unity 写入时序、游戏原生物理或源 PMX 精确还原。

写入在已核验的 ScriptRunBehaviourLateUpdate 返回后执行，目标与桥接父骨持有强 root，
每帧检查对象 / native / parent 身份并回读 localRotation；只写选定 Actor 的受控骨旋转与静态局部位置。
初始化与文件读取在 worker；高频回调使用预建固定容量表，无分配、文件 IO 或日志。
失败 / 停止 / 文件重载先停止写入并交还原生局部姿态；结束会话等待 hook 卸载并释放 roots。
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
# FK Basis A/B 实验

`MMD_TOOLS_BASIS_AB_TEST` 是可回退的实验模式。默认不存在 `motion_ab.txt` 时继续使用原播放路线。
诊断目录中的配置格式为 `legacy|basis identity|x|y|z|dance|frame N`，例如 `basis frame 0`。
使用 `basis sequence` 可运行约 40 秒的连续对照：Legacy/Basis Identity、Basis X/Y/Z、Legacy/Basis 固定帧 0/30/120。
开始与停止仍由用户操作；最后一段会保持，直到手动停止。每段独立缓存数值快照，停止后统一导出。
两路线共用 VMD 采样、FK 映射、静态 Mesh bind reference 与 LateUpdate 后的写入时序。
实验只驱动躯干、头颈、肩与双臂；真实 FK 链的中间骨保持静态参考。足 IK、腿轨、肩 P/C、捩骨、衣物头发从骨、位移、表情与相机不参与。

停止状态下，小键盘 **6** 选择 Legacy，**7** 选择 Basis，**8** 循环 Identity → 左上臂 X/Y/Z +30° → 固定帧 0/30/120。
仍由小键盘 **1** 或面板播放开始，**2** 或面板停止恢复；切换模式不会自动开始、停止或写入。
固定姿态会持续到手动停止。原始 VMD 第 0 帧保持不变。

Basis 使用 `C = inverse(bindModelRotation)`，`delta = C * qVMD * inverse(C)`，`local = bindLocal * delta`。
轴映射固定为 MMD XYZ → Unity 模型 XYZ，作为本次受控实验假设；模型朝向仍需要单轴画面验证。
Mesh 矩阵先按现有验证解码，拒绝非仿射、反射、shear 与奇异矩阵。
没有父骨 bind matrix 的骨仅进行模型空间锚定，诊断中的 `target_bind_local` 为 `null`；不会用现场动画伪造静态参考。
普通 Basis 对照只使用 Mesh bind reference；尼可的 `Bip001 Pelvis` 不在这些网格的蒙皮骨表中，因此该对照仍为部分 FK。

停止并完成主线程恢复后，worker 输出 `mmd_basis_ab.json` 与带路线、姿态、帧号和会话时间的副本，记录两路线局部旋转、角差、实际 Unity 回读、恢复与故障结果。
回读通过不代表视觉改善。`PoserProbeTests --basis` 验证 Identity、四元数符号不变性、独立 Rodrigues 单轴 oracle 和真实父层级组合；可额外输入只读骨架 fixture、VMD 与 JSON 输出路径。

## Pelvis 与下半身 FK 对照

诊断配置 `legacy fk_sequence` 在同一 VMD 的第 0、30、120 帧交替展示仅上半身和完整 FK，
每段 6 秒，最后一段保持。小键盘 1 开始、小键盘 2 停止恢复；本模式固定 Legacy，
不响应 6/7/8 的路线切换。腿部只使用足、膝、足首 FK 轨道；Center/Groove 位移、足 IK、
足 D、Grant 和 Root Motion 均不参与。`legacy fk_axes` 则自动展示静态参考及 Pelvis、Spine、
左右 Thigh 的 X/Y/Z 各 +30° 对照，每段 4 秒，仍由用户手动开始和停止。
完成对照后可用 `legacy fk_dance` 连续采样同一 VMD：使用完整 FK 范围，以 30 FPS 时间轴
播放，不再自动切换固定姿态。仍保留开场旋转，并排除位移、IK、Grant 与物理接管。

完整 FK 还检查 Body 的 `+PelvisTwist CF A01`：它与 Pelvis 并列挂在 Bip001 下，不能靠真实
父链自动跟随 Pelvis。存在该骨时，必须有有效 Body 静态参考、唯一名称、同 Actor 归属和
上述真实父层级，才将它别名映射到现有下半身 FK 增量；校验失败拒绝播放。这是腹部
蒙皮覆盖验证，不实现原生 twist 约束或 Grant。日志记录写入前的局部旋转，并在停止时
恢复该骨。腹部验证阶段的尼可为 19 个 VMD FK 来源、20 个目标映射、共 22 个写入骨。

准备阶段只读枚举同 Actor 下含 inactive 的 SMR，并逐个核验 Mesh bindposes；
这些网格仅用于诊断，不直接混合参考空间。普通播放和 FK 对照均要求取得
该 Animator 的 Avatar 静态 `m_DefaultPose`。当前样本通过唯一序列化签名、字段名称与
调用布局核验定位结构，并校验节点、CRC 名称/相对路径、真实父层级及读取稳定性。
仅接受覆盖所需祖先链的有效 TRS；将静态参考与全部 Body bindposes 在同一 Actor 模型空间
逐项比较通过后，才补充 Bip001/Pelvis 等未蒙皮参考及受控链的静态局部位置。
任何一步失败均拒绝播放，不以现场动画补值。

停止后输出 `pelvis_fk.*.json`，包含各阶段写入范围、静态模型/局部参考、VMD 采样、实际
Unity 局部/世界旋转、位置只读快照及数值误差。恢复覆盖本轮曾写入骨的并集，避免在仅
上半身阶段停止时遗漏先前写过的腿骨。参考可用和 setter 回读通过仍须与实机画面区分。
`PoserProbeTests --avatar` 验证静态 TRS、错误层级/尺度拒绝、FK 范围与阶段控制；可另传
本机只读 Avatar/Body fixture 核验真实静态数据。模型轴对齐仍需独立单轴实机确认。

## 双手手指 FK

完整 Legacy FK 现接管左右手各 15 个指节，尼可合计 52 个写入骨。
`親指０/１/２` 对应 `Finger0/Finger01/Finger02`；`人指/中指/薬指/小指１/２/３`
分别对应 `Finger1/2/3/4` 及各自后续两节。载入时核验十条 Hand→三指节的真实父链、
唯一节点、同 Actor 归属、有效静态几何，以及与 Body bindposes 对照通过的 Avatar DefaultPose。
拇指保持自己的静态旋转与位置，不复用其余四指的参考轴，也不额外翻转右手旋转。
尼可静态肩、臂、手部的模型朝向与标准 MMD 相差绕 Y 轴 180°；完整 FK 的全部源输入
统一使用 `S*q*S^-1`，即 `(-x,y,-z,w)`，并要求目标双手及拇指侧静态位置通过朝向校验。
身体和手指不能混用转换前后的坐标系。保留原始 VMD 四元数供日志对照，沿用 Legacy
组合，不对整段动作求逆，不交换左右骨；不匹配的骨架拒绝播放。`fk_axes` 的独立轴
测例仍定义于目标模型空间，旧的普通 Basis A/B 实验保留原设置。
缺少轨道的指节使用单位局部动作增量，继承父指节动作，并继续每帧写入静态参考对应的旋转，
避免残留原生手势。原始旋转只用于停止恢复。旧的普通 Basis A/B 范围保持原有定义。

诊断配置仍写入会话目录的 `motion_ab.txt`，开始和停止由用户手动控制：

- `legacy finger_test 386`：先保持静态参考，再依次双手拇指、食指、中指、无名指、小指，每段 4 秒。
  每段仅双手对应的指链使用指定 VMD 帧，其余骨保持静态参考；约 24 秒后按小键盘 2 恢复，最后一段会保持。
- `legacy finger_sequence 0 248 386`：依次展示三个指定帧，每段 6 秒，最后一段保持。
- `legacy fk_dance`：连续播放身体和手指，沿用现有 Legacy 重定向及 LateUpdate 返回后写入。

停止后 `pelvis_fk.*.json` 还记录指节轨道存在性、键数、阶段写入前旋转、会话内变化范围、
恢复四元数与逐骨恢复回读。可运行 `python SSMT-Poser/Tests/VerifyFingerFK.py <会话目录> <输出.json>`
独立核验 52 骨组合及 30 指节恢复；该脚本不代替单指屈伸与连续播放的实机视觉确认。
本阶段仍排除位移、足 IK/D、Grant 和物理接管。

## 次级骨实时物理与动作预烘焙

候选骨来自所选 Actor 的静态蒙皮分支：静态 Avatar DefaultPose 与 Body bindposes 交叉核验，
并核对同 Actor 的 SMR 蒙皮绑定。排除已受 VMD 控制的骨、Bip001 主骨、`+` 辅助骨及
Twist / Shape / Pos / Root / Rot 角色标记的辅助骨；其余有有效静态长度的蒙皮分支均可选择。
连续骨链默认选中，没有明确链结构和材质提示的单根骨标记“待确认”、默认不选。
名称只提示材质，不再决定候选资格：未知分支继承父链材质，无提示时使用保守衣物参数。
因此 Hem / Wing / Let 等分支或任意改名的连续链无需增加角色例外。
这仍是结构候选，不代表已读取游戏物理组件或源 PMX 刚体定义；静态层级无法证明单根骨的物理用途。
候选使用真实子骨方向；没有子骨时沿静态入骨方向估计末端，界面标记“末端估计”，零长骨不猜方向。
没有有效静态参考时保留游戏物理，不以开播时的现场衣物 / 头发姿态建立参考。

2026-10-14 根据第 75 次“迟钝僵硬、下垂塌陷、仍严重穿模”的反馈，替换逐骨粒子求解器，
使用 [Bullet 3.25](https://github.com/bulletphysics/bullet3/releases/tag/3.25) CPU 刚体与六自由度关节骨链。
刚体 / 骨骼偏移、动画碰撞体输入、整世界求解及动态 TRS 写回的流程参考
[Saba MMDPhysics](https://github.com/benikabocha/saba/blob/master/src/Saba/Model/MMD/MMDPhysics.cpp) 与
[Three.js r170 MMDPhysics](https://github.com/mrdoob/three.js/blob/r170/examples/jsm/animation/MMDPhysics.js)。
本项目使用 `btGeneric6DofSpring2Constraint`，没有复制两个参考项目的源码。
Bullet CPU 核心以 zlib 许可静态链接到 Poser；来源、锁定 commit 和逐文件 SHA-256 见
`ThirdParty/Bullet/README.md`、`LICENSE.txt`、`SOURCE.json`。构建不访问网络，无额外物理 DLL。

静态骨长推导胶囊、质量和关节；按静态子树各轴惯量设置角回形，扭转轴与弯曲轴分别计算。
短衣物按重力 / 骨长提高最低回形频率，避免统一参数使它直接塌到角限位。
角阻尼通过 Bullet 的零目标速度、限扭矩 motor 求解，空气阻力独立设置；衣物释放可稳定收敛。
相连物理骨共用关节求解，桥接骨沿静态 FK 继承。
物理分支根允许有限动态位移，接合位置以临界阻尼约束回到静态参考；链内保持静态局部骨长。
位置与旋转一起写回、烘焙和停止恢复，身体及腿部骨骼不进入物理写入路径。
碰撞冲量进入同一关节求解，去穿透修正速度限为 0.5 模型单位 / 秒，
骨骼写回后的几何另外执行受限接触投影。

模拟位于 Actor 模型空间，重力沿模型 -Y；动画身体球 / 胶囊随 FK / IK 移动。
动态骨胶囊之间也参与碰撞，共用接头的直接相邻骨和同父接合段不互撞。
身体代理保持名义半径。为完整衣物胶囊拟合一个共同静态偏移，偏移随骨旋转；
不移动静态骨根，不裁掉接合段，不以缩小身体或屏蔽碰撞对消除静态重叠。
FK 合回后再做受关节角限位约束的身体 / 衣物接触旋转修正，整条子树一起绕接头旋转，
并去掉继续朝接触内部的速度分量。
接头已经进入碰撞体时，回溯上游关节修正链形；衣物相撞时避免通过共同祖先同时旋转双方。
法线方向已到角限位时，检查其它关节轴的有限角候选，全部通过角限位和实际胶囊间距校验。
多碰撞体夹挤时，为整条分支联合拟合共同根位移，同时检查身体和其他衣物分支；
选择满足间距的较小位移，限制总位移半径，根位移继续传递到实际局部位置。
初始化 / 跳转也完成接触修正，第一帧不会直接发布相交代理。
每个 120 Hz 样本执行两个 240 Hz 固定子步，插值动画碰撞体；动态刚体启用球形 CCD。
写回后的几何重新检测接触，日志与缓存记录 `unresolved`、`max_penetration`、`max_merge_offset`，
最后一项是引擎刚体与固定骨长 FK 的最大位置差；烘焙统计不能用播放阶段的零实时接触替代。
FK / 接触投影移动刚体后，只清理受影响的接触 manifold，保留窄相 algorithm；
旧接触点不能在已经分离的形状之间继续热启动或被计为穿入。

身体形状仍由骨架估算，不能保证蒙皮表面无穿模。没有布料网格、源 PMX 刚体 / 约束 / 碰撞分组或 Grant；
骨胶囊自碰撞与球形 CCD 也不等于布料表面自碰撞或完整连续形状检测。
离线碰撞指标描述写回后代理形状，不能直接宣称实际蒙皮表面已经无穿模。
绑定所选骨架时，诊断目录中的 `native_physics_config.json` 自动保存该角色的原生粒子图、
碰撞引用与绑定时的局部缩放；游戏版本和字段消费者核验失败时记录原因，不猜测布局。
可读取的源 Mesh 顶点缓冲以原始格式保存，单 Mesh 最多 8 MiB、总量最多 16 MiB；
这属于绑定阶段的只读诊断，文件导出在 worker 执行。四权重布局、16 / 32 位索引格式及
64 字节子网格记录由对应消费者分别核验，并检查容量与三角形范围；未通过的布局明确记录失败。
这些源数据尚未作为插件的生产物理配置使用。离线验证可使用真实 inverse bind、权重和索引，
检查最终 FK 蒙皮三角形；代理胶囊的通过不能代替该检查。
接管发生在既有 LateUpdate 返回后，不调用未经核验的游戏物理 enable/disable 接口；
原生组件是否会在更晚阶段覆盖，以及画面中各部件的效果，须通过实机确认。

实时与预烘焙共用 `PhysicsMotion` 的纯静态 VMD FK、root motion、标准腿 / 脚尖 IK 采样及
120 Hz 采样。物理世界在 worker 预先建立，实时重播复用；库内部管理接触 / 求解缓冲。
受控父链使用静态局部位置；只选链首 / 链尾时，中间桥接骨在物理父骨之后写入
局部驱动，保持整条链的继承关系。实时播放最多补算 32 步，倒退或超过该上限的停顿会重置刚体，
防止游戏主线程长时间追帧。预烘焙在 worker 上使用不可变值数据计算，生成期间不读写 Unity 骨骼。
正常时间轴上，两者在 120 Hz 整数采样点一致。
接管物理时，身体、root motion、足 IK、衣物和表情统一使用当前 120 Hz 采样时刻，
所有模式共享开场混合；物理接管模式的身体 FK / IK 与碰撞输入使用同一 120 Hz 时刻。
未映射骨继承已经混合的父姿态与自己的 STATIC 局部旋转，不能逐层重复混合。
缓存采样 API 支持旋转 / 位移插值；面板播放采用统一采样时刻。
实时停顿重置后的轨迹可能不同。停止、异常或释放仍沿用原生姿态交还，排除 Poser 上一帧的写入值。

烘焙缓存位于 `%LOCALAPPDATA%/SSMT/Diagnostics/SSMT-Poser/PhysicsCache/*.ssmtpbin`。
缓存键包括静态层级与几何、骨骼选择、参数档、算法版本、完整动作及插值数据、源映射与 IK 设置。
变更会使内存烘焙失效；点击“生成 / 读取”后先校验缓存，再按需重新生成。文件记录帧数、骨索引、
120 Hz 局部旋转及相对静态局部位置的物理位移、碰撞统计与校验和，以临时文件写完后替换；
文件损坏时重新计算，缓存写入失败仍可播放已经生成的内存结果。
单个旋转及位移数据合计最多 64 MiB，最多 128 个物理骨 / 256 个模型节点；
取消不发布部分结果。更改求解语义时提高 `physics::Version`，更改驱动采样语义时提高
`physics::MotionDriverVersion`；两者分别进入缓存指纹，避免读取旧结果。

当前算法 / 缓存版本为 7，版本 1–6 自动失效；更换本版 DLL 后须重新点击“生成 / 读取”。
本版检查接触旋转涉及的整棵子树，避免接触候选顺带硬吸回其他轴的限位误差；
身体胶囊使用 STATIC 局部参考方向继承动画轴向转动，避免世界轴重建带来的虚假自转。
实际动作的真实蒙皮穿模与连续性验收仍未通过；这些修正不代表自有物理已经达到可交付效果。
当前身体驱动版本为 7。真实蒙皮检查保留在 `PoserPhysicsTests`，用源顶点、四权重、
STATIC inverse bind 和子网格三角形还原最终表面，分别报告代理穿入、真实相交与连续性。
STATIC 互斥按确切三角形对记录，已有相交不能豁免同一衣物三角形与其他身体面的新增相交。
未通过完整动作验收的表面碰撞实验已撤出编译目标，调查源码归档于本机忽略的临时目录。

`Validation/PhysicsTests.cpp` 覆盖真实偏转初始化后的衣物释放、水平短衣物不塌陷、稳定性、长度 / 摆动限制、稀疏骨链继承、实时 / 烘焙一致性、
重播 / 停顿、取消和缓存失效 / 损坏。`PhysicsMotionFixture.cpp` 另核验 mother / center / groove
及实际 Avatar / Body / VMD 输入。`PlayerGirlAmiceFixture.h` 保存第 73 次荧妹的静态 Spine2 / Amice
骨长与 Body inverse bind，验证 12 骨完整识别、转身后停稳与三个参数档的实时 / 烘焙一致性。
`OdetteBodyFixture.h` 保存第 74 次 156 个 Body 静态 bind，验证未知名称分支、改名后的候选与几何一致、
辅助骨排除和默认选择；另测骨段中间穿碰、移动身体、静态刚体偏移与世界旋转无关、
相反法线夹挤、衣物分支相交、动态根位移烘焙 / 恢复接合、第一帧接触、位移缓存越界，
以及写回后接触和不可行冲突记录。
实际荧 Avatar / Body / 《桃花笑》夹具按静态 CRC 补齐无 Body 蒙皮权重的上臂骨，
使用完整 10 个身体碰撞体，验证整段缓存读写和局部 TRS 还原。旧第 6 版曾通过代理穿入检查，
但第 78 次反馈及缓存检查证实仍有明显跳变；新增加的连续性检查会对此失败。
不能将代理穿入数值作为实际蒙皮表面已修复的证明。
具体数值与适用范围见 `Reports/2026-10-14-Physics-Collision-Writeback.md`。
`PhysicsUiTests.cpp` 验证模式、逐骨选择、烘焙播放门控、
停止优先、取消和释放。以上均不替代真实 Unity 写入时序、恢复与视觉验证。

光标接管同时处理 `ShowCursor` 显隐计数，并在游戏窗口所属输入线程刷新，
防止 UI 线程解锁后游戏线程的光标仍隐藏。跨线程回归核验真实计数与解除接管后的恢复。
身体驱动版本为 7：所有物理模式共享开场混合；静态同父级、同枢轴且有唯一受控关节的
未命名蒙皮辅助叶骨跟随最终 IK，并保留自身静态朝向。实机视觉效果仍待验证。

## 实机验证归档

[2026-10-08 Pelvis、下半身 FK 与腹部辅助骨验证](Reports/2026-10-08-Pelvis-FK.md)
记录静态参考来源、固定帧和单轴对照、最终连续播放及恢复结果；附可独立复核的关键骨旋转数据。

## VMD 表情

VMD 形态键保留名字、帧与权重，按 MMD 的线性权重在动作 30 fps 时间轴采样。
动作自带形态键时自动使用；没有时尝试同目录的“动作名前缀-表情.vmd”，也可在面板单独载入。
加载其他动作时会重新寻找其配套表情；用户手动载入的表情保持优先。
文件可只有表情，没有骨骼轨道。

绑定时通过同 Actor 的 SMR 读取实际 Mesh 形态键名称，接口须通过元数据和原生 ABI 指纹验证。
同名匹配及明确的 blink 导出名称可以自动映射，其他差异在面板选择源 / 目标并映射，
同一源可驱动左右两个目标；不能直接将 MMD 模型的形态键编号套到游戏模型。

面板提供“保留游戏原有表情”“覆盖已映射形态键”和“替换表情：其余形态键归零”。
替换模式只接管至少有一个映射目标的网格，在这些网格中将未映射键清零，防止原有表情叠加；
其它网格保持原生控制。权重从 VMD 的 0..1 转为 Unity 的 0..100，零权重也会写入。
停止交还最新原生权重，排除 Poser 自己的上一帧写值；角色 / 网格身份变化后禁止写入旧对象。
表情故障不阻止原有骨骼恢复。

若 Mesh 不提供形态键，面板明确显示当前角色没有可访问的形态键，不能用材质 UV 或未知字段冒充成功。
源模型的材质表情、复合 morph 和骨骼表情不会自动转换；“两目.vmd”是独立骨骼动作，
本表情面板负责 morph 权重，不把眼球骨骼键误当作形态键。

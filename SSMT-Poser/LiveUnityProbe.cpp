#include "LiveUnityProbe.h"
#include "RigProbe.h"
#include "BoneWriteProbe.h"
#include "MotionPlayback.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <fstream>
#include <ostream>
#include <string>

namespace poser {
namespace {
template<class T> bool Read(uintptr_t address, T& value) {
    SIZE_T count = 0;
    return address && address <= UINTPTR_MAX - sizeof(T) &&
        ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), &value, sizeof(T), &count) && count == sizeof(T);
}
bool Name(uintptr_t klass, const char* expected) {
    uintptr_t p = 0;
    if (!Read(klass + 0x28, p)) return false;
    for (size_t i = 0; i <= std::strlen(expected); ++i) {
        char c = 0;
        if (!Read(p + i, c) || c != expected[i]) return false;
    }
    return true;
}
uintptr_t Relative(uintptr_t instruction, size_t displacement, size_t length) {
    int32_t d = 0;
    return Read(instruction + displacement, d) ? native_detail::RelativeTarget(instruction, d, length) : 0;
}
bool Pattern(uintptr_t address, size_t size, const char* signature) {
    std::vector<uint8_t> bytes(size);
    SIZE_T count = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), bytes.data(), size, &count) || count != size) return false;
    return native_detail::FindPattern(bytes, signature).size() == 1;
}
struct Bone {
    uintptr_t object = 0, klass = 0, native = 0;
    int nameLength = 0;
    wchar_t name[128]{};
};
struct Item {
    uintptr_t klass = 0, descriptor = 0, reflection = 0, reflectionDescriptor = 0, repeat = 0, array = 0, arrayClass = 0;
    uintptr_t object = 0, objectClass = 0, nativeObject = 0, transform = 0, gameObject = 0, nameObject = 0;
    uint32_t metadataId = 0, handles[7]{};
    unsigned rooted = 0, released = 0;
    unsigned skippedTransforms = 0;
    uint64_t length = 0;
    int childCount = -1, nativeChildCount = -1, nameLength = 0;
    wchar_t name[128]{};
    bool arrayValid = false, liveValid = false;
    uintptr_t rootBone = 0, bonesArray = 0, sharedMesh = 0;
    uint64_t bonesLength = 0;
    int nativeBonesLength = -1, meshNameLength = 0;
    wchar_t meshName[128]{};
    bool isHuman = false, humanRead = false, bonesValid = false;
    std::array<Bone, 512> bones;
};
struct Job {
    NativeCallContext context;
    uintptr_t find = 0, findMethod = 0, child = 0, childMethod = 0;
    uintptr_t transform = 0, transformMethod = 0, gameObject = 0, gameObjectMethod = 0, name = 0, nameMethod = 0;
    uintptr_t transformClass = 0, gameObjectClass = 0, reflectionClass = 0;
    NativeMethod human, rootBone, bones, mesh;
    DWORD mainThread = 0, actualThread = 0, fault = 0;
    uintptr_t runtimeThread = 0;
    unsigned step = 0;
    unsigned mode = 0;
    bool complete = false;
    std::array<Item, 3> items;
};
// 任务数据常驻插件；各次 callback 只执行有界只读任务，不跨帧保留 managed 引用。
Job job;
std::atomic<unsigned> state{0}, active{0};
UINT dispatchMessage = 0;
using Getter = uintptr_t (*)(uintptr_t, uintptr_t);
using IntGetter = int (*)(uintptr_t, uintptr_t);
using Factory = uintptr_t (*)(uintptr_t);
using NewHandle = uint32_t (*)(uintptr_t, bool);
using TargetHandle = uintptr_t (*)(uint32_t);
using FreeHandle = void (*)(uint32_t);

bool Root(Item& item, unsigned slot, uintptr_t object) {
    if (!object) return false;
    item.handles[slot] = reinterpret_cast<NewHandle>(job.context.handleNew)(object, false);
    const auto valid = item.handles[slot] && (item.handles[slot] >> 26) == 2 &&
        reinterpret_cast<TargetHandle>(job.context.handleTarget)(item.handles[slot]) == object;
    if (valid) ++item.rooted;
    return valid;
}
bool Alive(uintptr_t object, uintptr_t expected, uintptr_t& native) {
    uintptr_t klass = 0;
    return Read(object, klass) && klass == expected && Read(object + 0x10, native) && native;
}
bool ReadName(uintptr_t object, wchar_t (&name)[128], int& length) {
    const auto text = reinterpret_cast<Getter>(job.name)(object, job.nameMethod);
    uintptr_t klass = 0;
    if (!Read(text, klass) || !Name(klass, "String") || !Read(text + 0x10, length) || length < 0 || length >= 128) return false;
    for (int i = 0; i < length; ++i) if (!Read(text + 0x14 + size_t(i) * 2, name[i])) return false;
    return true;
}
// 此叶函数没有 C++ 对象析构；记录 SEH 码后停止调用，不把异常穿过 Windows callback。
bool ExecuteGuarded() {
    __try {
        job.step = 1;
        job.actualThread = GetCurrentThreadId();
        DWORD expected = 0;
        if (!Read(job.context.mainThreadSlot, expected) || expected != job.actualThread || expected != job.mainThread) return false;
        job.runtimeThread = reinterpret_cast<uintptr_t (*)()>(job.context.threadCurrent)();
        if (!job.runtimeThread) return false;
        if (job.mode) { job.step=20+job.mode; job.complete=job.mode>=6?MotionMainStep(job.mode):BoneWriteStep(job.mode); return job.complete; }
        BeginRigSelection();
        for (size_t target = 1; target < job.items.size(); ++target) {
            auto& item = job.items[target];
            job.step = 2;
            item.reflection = reinterpret_cast<Factory>(job.context.reflectionType)(item.descriptor);
            if (!Read(item.reflection + 0x10, item.reflectionDescriptor) ||
                !live_detail::ValidateReflection(item.reflection, job.reflectionClass, item.descriptor) || !Root(item, 0, item.reflection)) return false;
            item.repeat = reinterpret_cast<Factory>(job.context.reflectionType)(item.descriptor);
            if (item.repeat != item.reflection) return false;
            job.step = 3;
            item.array = reinterpret_cast<Getter>(job.find)(item.reflection, job.findMethod);
            if (!Root(item, 1, item.array)) return false;
            item.arrayValid = live_detail::ValidateArray(item.array, item.klass, item.length);
            if (!item.arrayValid || !Read(item.array, item.arrayClass)) return false;
            if (!item.length) continue;
            job.step = 4;
            int largestBones = -1;
            for (uint64_t i = 0; i < std::min<uint64_t>(item.length, 256); ++i) {
                uintptr_t object = 0, klass = 0;
                if (!Read(item.array + 0x20 + i * 8, object) || !Read(object, klass)) return false;
                if (klass != item.klass) continue;
                uintptr_t native = 0;
                int count = 0;
                if (!Alive(object, item.klass, native)) return false;
                if (target == 2) {
                    const auto transform = reinterpret_cast<Getter>(job.transform)(object, job.transformMethod);
                    uintptr_t nativeTransform = 0;
                    if (!Alive(transform, job.transformClass, nativeTransform)) { ++item.skippedTransforms; continue; }
                    item.object = object; item.objectClass = klass;
                    break;
                }
                // 已验证 get_bones consumer 从 native+430 容器的 +10 读取骨数量。
                if (!Read(native + 0x440, count) || count < 0) return false;
                if (RigHasTarget()) {
                    const auto match=MatchRigCandidate(object);
                    if (match<0) return false;
                    if (!match) continue;
                }
                // 在有界候选集合中优先骨数量最多且可完整导出的 SMR，不用名称猜角色身份。
                if (count <= int(item.bones.size()) && count > largestBones) {
                    item.object = object; item.objectClass = klass; largestBones = count;
                }
            }
            // 同一数组可能含 RectTransform 等派生实例，本阶段只取可直接交叉核验的精确类型。
            if (!item.object) continue;
            if (!Alive(item.object, item.klass, item.nativeObject)) return false;
            job.step = 5;
            // 已确认 childCount 返回 EAX，并与 native 字段交叉核验。
            item.transform = item.klass == job.transformClass ? item.object :
                reinterpret_cast<Getter>(job.transform)(item.object, job.transformMethod);
            uintptr_t nativeTransform = 0;
            if (!Alive(item.transform, job.transformClass, nativeTransform) || !Root(item, 2, item.transform)) return false;
            item.childCount = reinterpret_cast<IntGetter>(job.child)(item.transform, job.childMethod);
            if (!Read(nativeTransform + 0x90, item.nativeChildCount) || item.childCount < 0 || item.childCount > 100000 || item.childCount != item.nativeChildCount) return false;
            job.step = 6;
            item.gameObject = reinterpret_cast<Getter>(job.gameObject)(item.object, job.gameObjectMethod);
            uintptr_t nativeGameObject = 0;
            if (!Alive(item.gameObject, job.gameObjectClass, nativeGameObject) || !Root(item, 3, item.gameObject)) return false;
            job.step = 7;
            item.nameObject = reinterpret_cast<Getter>(job.name)(item.gameObject, job.nameMethod);
            uintptr_t stringClass = 0;
            if (!Read(item.nameObject, stringClass) || !Name(stringClass, "String") || !Read(item.nameObject + 0x10, item.nameLength) ||
                item.nameLength < 0 || item.nameLength >= 128) return false;
            for (int i = 0; i < item.nameLength; ++i) if (!Read(item.nameObject + 0x14 + size_t(i) * 2, item.name[i])) return false;
            item.liveValid = true;
            if (target == 2) {
                job.step = 9;
                item.isHuman = reinterpret_cast<bool (*)(uintptr_t, uintptr_t)>(job.human.entry)(item.object, job.human.descriptor);
                item.humanRead = true;
            } else {
                job.step = 10;
                item.rootBone = reinterpret_cast<Getter>(job.rootBone.entry)(item.object, job.rootBone.descriptor);
                uintptr_t nativeRoot = 0;
                if (item.rootBone && (!Alive(item.rootBone, job.transformClass, nativeRoot) || !Root(item, 4, item.rootBone))) return false;
                job.step = 11;
                item.bonesArray = reinterpret_cast<Getter>(job.bones.entry)(item.object, job.bones.descriptor);
                if (!Root(item, 5, item.bonesArray) || !live_detail::ValidateArray(item.bonesArray, job.transformClass, item.bonesLength) ||
                    !Read(item.nativeObject + 0x440, item.nativeBonesLength) || item.nativeBonesLength < 0 ||
                    item.bonesLength != uint64_t(item.nativeBonesLength) || item.bonesLength > item.bones.size()) return false;
                item.bonesValid = true;
                job.step = 12;
                item.sharedMesh = reinterpret_cast<Getter>(job.mesh.entry)(item.object, job.mesh.descriptor);
                if (item.sharedMesh) {
                    uintptr_t meshClass = 0, nativeMesh = 0;
                    if (!Read(item.sharedMesh, meshClass) || !Name(meshClass, "Mesh") || !Read(item.sharedMesh + 0x10, nativeMesh) || !nativeMesh ||
                        !Root(item, 6, item.sharedMesh) || !ReadName(item.sharedMesh, item.meshName, item.meshNameLength)) return false;
                }
                job.step = 13;
                for (size_t i = 0; i < item.bonesLength; ++i) {
                    auto& bone = item.bones[i];
                    if (!Read(item.bonesArray + 0x20 + i * 8, bone.object)) return false;
                    // 保留 null slot 的索引，不能对空/已销毁 Transform 调用 getter。
                    if (!bone.object) continue;
                    if (!Read(bone.object, bone.klass) || !Alive(bone.object, job.transformClass, bone.native) ||
                        !ReadName(bone.object, bone.name, bone.nameLength)) return false;
                }
            }
        }
        job.step = 14;
        const auto& smr = job.items[1];
        const auto& animator = job.items[2];
        if (smr.liveValid && smr.bonesValid && smr.bonesLength && !CaptureRig(smr.object, smr.transform, smr.bonesArray,
            smr.bonesLength, smr.array, smr.length, animator.array, animator.length)) return false;
        job.step = 8;
        job.complete = true;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        job.fault = GetExceptionCode();
        return false;
    }
}
void ReleaseGuarded() {
    __try {
        ReleaseBoneScratch();
        if (!job.complete && MotionRigArmed()) { DisableMotionWrites(); MotionMainStep(6); }
        if (!job.complete && BoneWriteArmed()) BoneWriteStep(4);
        ReleaseBoneScratch();
        ReleaseRigProbe();
        for (auto& item : job.items) for (auto& handle : item.handles) {
            if (handle) { reinterpret_cast<FreeHandle>(job.context.handleFree)(handle); ++item.released; }
            handle = 0;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { job.fault = GetExceptionCode(); job.complete = false; }
}
LRESULT CALLBACK Dispatch(int code, WPARAM wParam, LPARAM lParam) {
    active.fetch_add(1, std::memory_order_acq_rel);
    if (code >= 0 && wParam == PM_REMOVE) {
        const auto* message = reinterpret_cast<const MSG*>(lParam);
        unsigned expected = 1;
        if (message && message->message == dispatchMessage && message->wParam == reinterpret_cast<WPARAM>(&job) &&
            state.compare_exchange_strong(expected, 2, std::memory_order_acq_rel)) {
            ExecuteGuarded();
            ReleaseGuarded();
            state.store(3, std::memory_order_release);
        }
    }
    const auto result = CallNextHookEx(nullptr, code, wParam, lParam);
    active.fetch_sub(1, std::memory_order_release);
    return result;
}
struct Window { DWORD thread; HWND hwnd; };
BOOL CALLBACK SelectWindow(HWND hwnd, LPARAM parameter) {
    auto& result = *reinterpret_cast<Window*>(parameter);
    DWORD pid = 0;
    const auto tid = GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId() && tid == result.thread && IsWindowVisible(hwnd) && !GetWindow(hwnd, GW_OWNER)) {
        result.hwnd = hwnd; return FALSE;
    }
    return TRUE;
}
bool DispatchJob(HANDLE stop,HMODULE module,Window window,std::ostream& report,bool cancellable) {
    job.complete=false; job.fault=0; job.step=0;
    auto hook=SetWindowsHookExW(WH_GETMESSAGE,Dispatch,module,job.mainThread);
    if (!hook) { report << "dispatch_hook_error=" << GetLastError() << '\n'; return false; }
    state.store(1,std::memory_order_release);
    if (!PostMessageW(window.hwnd,dispatchMessage,reinterpret_cast<WPARAM>(&job),0)) state.store(0,std::memory_order_release);
    const auto deadline=GetTickCount64()+30000;
    while (state.load(std::memory_order_acquire)!=3 && GetTickCount64()<deadline) {
        if (cancellable && WaitForSingleObject(stop,10)==WAIT_OBJECT_0) break;
        if (!cancellable) Sleep(10);
    }
    unsigned pending=1;
    state.compare_exchange_strong(pending,0,std::memory_order_acq_rel);
    while (state.load(std::memory_order_acquire)==2) Sleep(1);
    const auto removed=UnhookWindowsHookEx(hook);
    const auto error=removed?0:GetLastError();
    while (active.load(std::memory_order_acquire)) Sleep(1);
    if (!removed) {
        HMODULE pinned=nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Dispatch),&pinned);
        state.store(0,std::memory_order_release);
        report << "dispatch_unhook=FAILED error=" << error << " module_pinned=" << bool(pinned) << '\n';
        return false;
    }
    report << "dispatch_unhook=OK; callbacks_in_flight=0\ndispatch_state=" << state.load() << " actual_tid=" << job.actualThread
        << " Unity_main_tid=" << job.mainThread << " step=" << job.step << " SEH=0x" << std::hex << job.fault << std::dec << '\n';
    return job.complete && !job.fault && state.load()==3;
}
bool RunMotionSession(GenshinNativeRuntime& runtime,HANDLE stop,HMODULE module,Window window,std::ostream& report,const std::filesystem::path& directory) {
    if (!MotionRigArmed()) { report << "motion_session=NOT_ARMED; reason=Actor_or_rig_gate\n"; return false; }
    bool passed=InstallAnimationReturnProbe(runtime,stop,report);
    report << "phase=MOTION_COMMANDS_READY; F11=load_or_reload_and_play; F12=stop_and_restore; F7=finish_session\n";
    report << "motion_path_file=motion_path.txt; command_flags=motion_play.flag,motion_stop.flag,motion_exit.flag\n"; report.flush();
    auto consume=[&](const wchar_t* name) {
        const auto path=directory/name;
        if (GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES) return false;
        return DeleteFileW(path.c_str())!=0;
    };
    bool previousPlay=false,previousStop=false,previousExit=false;
    try {
        while (passed && WaitForSingleObject(stop,25)!=WAIT_OBJECT_0) {
            DWORD foreground=0; GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
            const auto inGame=foreground==GetCurrentProcessId();
            const bool play=inGame&&(GetAsyncKeyState(VK_F11)&0x8000),stopKey=inGame&&(GetAsyncKeyState(VK_F12)&0x8000),finish=inGame&&(GetAsyncKeyState(VK_F7)&0x8000);
            const bool playCommand=(play&&!previousPlay)||consume(L"motion_play.flag"),stopCommand=(stopKey&&!previousStop)||consume(L"motion_stop.flag"),exitCommand=(finish&&!previousExit)||consume(L"motion_exit.flag");
            previousPlay=play; previousStop=stopKey; previousExit=finish;
            if (exitCommand) break;
            if (stopCommand || MotionStatus()==2 || MotionStatus()==3 || playCommand) {
                const auto prior=MotionStatus();
                DisableMotionWrites(); job.mode=8;
                passed=DispatchJob(stop,module,window,report,false);
                ReportMotion(report);
                report << "motion_stop=" << (passed?"RESTORED":"FAILED") << " prior_status=" << prior << '\n'; report.flush();
                if (prior==3) { passed=false; break; }
            }
            if (playCommand && passed) {
                std::ifstream pathFile(directory/L"motion_path.txt",std::ios::binary);
                std::string text; if (pathFile) std::getline(pathFile,text);
                if (!text.empty()&&text.back()=='\r') text.pop_back();
                if (text.starts_with("\xEF\xBB\xBF")) text.erase(0,3);
                const auto size=text.size()<=32767?MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0):0;
                if (!size) { report << "motion_load=REJECTED; reason=MOTION_PATH_EMPTY_OR_INVALID_UTF8\n"; report.flush(); continue; }
                std::wstring path(size,L'\0'); MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),path.data(),size);
                report << "motion_source=" << text << '\n';
                if (!LoadMotionClip(path,report,stop)) continue;
                job.mode=7; passed=DispatchJob(stop,module,window,report,true);
                report << "motion_start=" << (passed?"PLAYING":"FAILED") << '\n'; report.flush();
            }
        }
    } catch (const std::exception& error) { passed=false; report << "motion_worker_error=" << error.what() << '\n'; }
    DisableMotionWrites();
    if (MotionRigArmed()) { job.mode=8; passed=DispatchJob(stop,module,window,report,false)&&passed; ReportMotion(report); }
    passed=RemoveAnimationReturnProbe(report)&&passed;
    if (MotionRigArmed()) { job.mode=6; passed=DispatchJob(stop,module,window,report,false)&&passed; }
    if (BoneWriteArmed()) { job.mode=4; passed=DispatchJob(stop,module,window,report,false)&&passed; }
    ReportMotion(report); ReportBoneWrite(report); ReportAnimationReturnProbe(report);
    report << "motion_session=" << (passed?"STOPPED_AND_RELEASED":"FAILED") << "; visual_confirmation=USER_OBSERVATION_REQUIRED\n"; report.flush();
    return passed;
}
bool RunBoneExperiment(GenshinNativeRuntime& runtime,HANDLE stop,HMODULE module,Window window,std::ostream& report,const std::filesystem::path& directory) {
    if (!BoneWriteArmed()) { report << "write_result=NOT_ARMED; reason=target_or_rig_gate\n"; return true; }
    const auto arm=IsArmExperiment();
    report << "phase=" << (arm?"WAITING_ARM_WRITE; trigger=foreground_F10_or_write_ready.flag":"WAITING_HEAD_WRITE; trigger=foreground_F9_or_write_ready.flag") << '\n';
    ReportBoneWrite(report); report.flush();
    const auto marker=directory/L"write_ready.flag";
    const auto deadline=GetTickCount64()+5*60*1000;
    bool trigger=false;
    while (GetTickCount64()<deadline && WaitForSingleObject(stop,100)!=WAIT_OBJECT_0) {
        DWORD foreground=0; GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
        if ((foreground==GetCurrentProcessId() && (GetAsyncKeyState(arm?VK_F10:VK_F9)&0x8000)) || GetFileAttributesW(marker.c_str())!=INVALID_FILE_ATTRIBUTES) { trigger=true; break; }
    }
    bool passed=true;
    if (trigger) {
        report << "character_writes=" << (arm?"LEFT_UPPERARM_LOCAL_ROTATION_ONLY; amplitude_degrees=60":"HEAD_LOCAL_ROTATION_ONLY; amplitude_degrees=4") << "; Animator_disable=NO\n";
        // 上臂实验执行两次独立的修改/恢复，中间留出正常动画对照间隔。
        if (arm) passed=InstallAnimationReturnProbe(runtime,stop,report);
        for (unsigned cycle=0;passed && cycle<(arm?2u:1u);++cycle) {
            report << "[WRITE_CYCLE] index=" << cycle << '\n';
            job.mode=1; passed=DispatchJob(stop,module,window,report,true);
            if (arm && passed) EnableAnimationReturnWrites(true);
            if (passed && WaitForSingleObject(stop,250)!=WAIT_OBJECT_0) { job.mode=2; passed=DispatchJob(stop,module,window,report,true); }
            if (arm) {
                if (passed) WaitForSingleObject(stop,1700);
                EnableAnimationReturnWrites(false);
            } else {
                for (unsigned i=0;passed && i<30 && WaitForSingleObject(stop,33)!=WAIT_OBJECT_0;++i) {
                    job.mode=3; passed=DispatchJob(stop,module,window,report,true);
                }
            }
            if (arm && cycle==0 && passed) {
                job.mode=5; passed=DispatchJob(stop,module,window,report,false);
                if (WaitForSingleObject(stop,1000)==WAIT_OBJECT_0) break;
            }
        }
        if (arm) {
            EnableAnimationReturnWrites(false);
            passed=RemoveAnimationReturnProbe(report) && passed;
            ReportAnimationReturnProbe(report);
            passed=AnimationReturnWritesConfirmed() && passed;
        }
    }
    // 取消也要向同一主线程派发恢复；恢复派发不因已经 signaled 的 stopEvent 提前退出。
    if (BoneWriteArmed()) { job.mode=4; passed=DispatchJob(stop,module,window,report,false) && passed; }
    ReportBoneWrite(report);
    report << "write_result=" << (trigger && passed?"READBACK_AND_RESTORE_CONFIRMED":trigger?"FAILED":"NOT_TRIGGERED_CANCELLED_OR_TIMEOUT")
        << "; visual_confirmation=USER_OBSERVATION_REQUIRED; callback="
        << (arm?"SCRIPT_LATEUPDATE_RETURN_TIMING_EXPERIMENT":"WINDOW_MESSAGE_NO_LATE_PHASE_CLAIM") << '\n';
    report.flush();
    return passed;
}
bool Method(GenshinNativeRuntime& runtime, const char* owner, const char* name, uint8_t parameters, NativeMethod& result) {
    NativeClass klass;
    return runtime.FindClass("UnityEngine", owner, klass) && runtime.FindMethod(klass, name, parameters, result) &&
        result.descriptor && result.entry && result.target;
}
void Address(std::ostream& report, const char* key, uintptr_t value) { report << key << "=0x" << std::hex << value << std::dec << '\n'; }
std::string Utf8(const wchar_t* text, int length) {
    const auto size = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    if (size) WideCharToMultiByte(CP_UTF8, 0, text, length, result.data(), size, nullptr, nullptr);
    return result;
}
}
namespace live_detail {
bool ValidateReflection(uintptr_t object, uintptr_t runtimeClass, uintptr_t descriptor) {
    uintptr_t klass = 0, type = 0;
    std::array<uint8_t, 16> actual{}, expected{};
    // 反射缓存按类型内容比较：缓存可引用 registration descriptor，class 保存的是其副本。
    return object && runtimeClass && descriptor && Read(object, klass) && klass == runtimeClass &&
        Read(object + 0x10, type) && Read(type, actual) && Read(descriptor, expected) && actual == expected;
}
bool ValidateArray(uintptr_t array, uintptr_t elementClass, uint64_t& length) {
    uintptr_t klass = 0, bounds = 1, elementDescriptor = 0;
    uint32_t actualId = UINT32_MAX, expectedId = UINT32_MAX;
    // 当前 profile 的 allocator 在 +18 写长度、枚举器在 +20 写引用。仅接受 SZARRAY。
    uint8_t type = 0;
    if (!array || !elementClass || !Read(array, klass) || !Read(klass + 0x68 + 0xA, type) || type != 0x1D ||
        !Read(array + 0x10, bounds) || bounds || !Read(array + 0x18, length) || length > 1000000) return false;
    return Read(klass + 0x68, elementDescriptor) && Read(elementDescriptor, actualId) &&
        Read(elementClass + 0x68, expectedId) && actualId == expectedId;
}
}
bool ProbeLiveUnity(GenshinNativeRuntime& runtime, HANDLE stop, std::ostream& report, const std::filesystem::path& directory) {
    job = Job{};
    if (!runtime.ResolveCallContext(stop, job.context, report)) return false;
    NativeClass reflection;
    if (!Read(job.context.reflectionClassSlot, job.reflectionClass) || !runtime.DescribeClass(job.reflectionClass, reflection) ||
        reflection.namespaze != "System" || reflection.name != "RuntimeType") {
        report << "blocker=RuntimeType class identity\n"; return false;
    }
    Address(report, "RuntimeType_class", job.reflectionClass);
    report << "RuntimeType_metadataId=" << reflection.metadataId << '\n';
    const std::array names = {"Transform", "SkinnedMeshRenderer", "Animator"};
    for (size_t i = 0; i < names.size(); ++i) {
        NativeClass klass;
        if (!runtime.FindClass("UnityEngine", names[i], klass)) return false;
        job.items[i].klass = klass.address; job.items[i].metadataId = klass.metadataId;
        job.items[i].descriptor = klass.address + 0x68;
        uint32_t id = 0; uint8_t kind = 0;
        if (!Read(klass.address + 0x68, id) || id != klass.metadataId || !Read(klass.address + 0x72, kind) || kind != 0x12) {
            report << "blocker=class by-value descriptor identity\n"; return false;
        }
    }
    job.transformClass = job.items[0].klass;
    NativeClass go;
    if (!runtime.FindClass("UnityEngine", "GameObject", go)) return false;
    job.gameObjectClass = go.address;
    NativeMethod find, child, transform, gameObject, name;
    if (!Method(runtime, "Object", "FindObjectsOfType", 1, find) || !Method(runtime, "Transform", "get_childCount", 0, child) ||
        !Method(runtime, "Component", "get_transform", 0, transform) || !Method(runtime, "Component", "get_gameObject", 0, gameObject) ||
        !Method(runtime, "Object", "get_name", 0, name)) {
        report << "blocker=typed method descriptor\n"; return false;
    }
    // 验证实际 callee 数据流，不依据公共头文件臆测隐藏参数与 managed array 布局。
    if (!Pattern(find.target, 32, "48 83 EC 28 48 8B D1 41 B8 01 00 00 00 48 8D 4C 24 30 E8 ?? ?? ?? ?? 48 8B 00") ||
        !Pattern(child.target, 96, "8B 80 90 00 00 00") ||
        !Pattern(transform.target, 80, "48 8B 50 40") || !Pattern(gameObject.target, 112, "48 8B 78 40") ||
        !Pattern(name.target, 224, "E9 ?? ?? ?? ??")) {
        report << "blocker=typed callee dataflow\n"; return false;
    }
    const auto enumeration = Relative(find.target + 18, 1, 5);
    if (!Pattern(enumeration, 0x440, "49 8B 17 4A 89 54 39 20")) {
        report << "blocker=array element writer not recovered\n"; return false;
    }
    // 由枚举器的数组分配 call 找到 allocator，再验证长度写入及字符串构造字段。
    std::vector<uint8_t> enumerationBytes(0x440);
    SIZE_T copied = 0;
    ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(enumeration), enumerationBytes.data(), enumerationBytes.size(), &copied);
    const auto arrayCalls = native_detail::FindPattern(enumerationBytes, "48 8B CB 48 8B D6 E8 ?? ?? ?? ?? 45 85 F6");
    if (copied != enumerationBytes.size() || arrayCalls.size() != 1) return false;
    const auto arrayWrapper = Relative(enumeration + arrayCalls[0] + 6, 1, 5);
    const auto arrayAllocator = Relative(arrayWrapper + 32, 1, 5);
    if (!Pattern(arrayWrapper, 37, "48 89 C1 48 89 F2 48 83 C4 20 5E E9 ?? ?? ?? ??") ||
        !Pattern(arrayAllocator, 0x180, "48 89 58 18")) { report << "blocker=array length writer\n"; return false; }
    // Object.get_name 尾调用的 string constructor 自身负责 UTF-8 到 managed UTF-16 转换。
    std::vector<uint8_t> nameBytes(224);
    ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(name.target), nameBytes.data(), nameBytes.size(), &copied);
    const auto tail = native_detail::FindPattern(nameBytes, "5B E9 ?? ?? ?? ??");
    if (tail.size() != 1) return false;
    const auto stringConstructor = Relative(name.target + tail[0] + 1, 1, 5);
    if (!Pattern(stringConstructor, 0xC0, "89 78 10 01 FF 4C 63 C7") ||
        !Pattern(stringConstructor, 0xC0, "48 8D 4B 14")) { report << "blocker=managed string writer\n"; return false; }
    Address(report, "array_allocator", arrayAllocator); Address(report, "string_constructor", stringConstructor);
    job.find = find.entry; job.findMethod = find.descriptor; job.child = child.entry; job.childMethod = child.descriptor;
    job.transform = transform.entry; job.transformMethod = transform.descriptor;
    job.gameObject = gameObject.entry; job.gameObjectMethod = gameObject.descriptor; job.name = name.entry; job.nameMethod = name.descriptor;
    if (!Method(runtime, "Animator", "get_isHuman", 0, job.human) ||
        !Method(runtime, "SkinnedMeshRenderer", "get_rootBone", 0, job.rootBone) ||
        !Method(runtime, "SkinnedMeshRenderer", "get_bones", 0, job.bones) ||
        !Method(runtime, "SkinnedMeshRenderer", "get_sharedMesh", 0, job.mesh)) {
        report << "blocker=scene getter descriptors\n"; return false;
    }
    if (!Pattern(job.bones.target, 100, "48 8D B8 30 04 00 00") ||
        !Pattern(job.bones.target, 100, "48 63 57 10") ||
        !Pattern(job.rootBone.target, 60, "48 8B C8 E8 ?? ?? ?? ?? 48 8B D0 48 8D 4C 24 30 E8 ?? ?? ?? ?? 48 8B 00") ||
        !Pattern(job.mesh.target, 60, "48 8B C8 E8 ?? ?? ?? ?? 48 8B D0 48 8D 4C 24 30 E8 ?? ?? ?? ?? 48 8B 00") ||
        !Pattern(job.human.target, 48, "48 8B C8 48 83 C4 20 5B E9 ?? ?? ?? ??")) {
        report << "blocker=scene getter callee dataflow\n"; return false;
    }
    for (const auto& method : {job.human, job.rootBone, job.bones, job.mesh}) {
        report << "[SCENE_GETTER] " << method.name << " metadataId=" << method.metadataId << '\n';
        Address(report, "entry", method.entry); Address(report, "target", method.target); Address(report, "descriptor", method.descriptor);
    }
    RigCalls rigCalls{job.context, job.transformClass, job.gameObjectClass, job.name, job.nameMethod,
        job.transform, job.transformMethod, job.gameObject, job.gameObjectMethod, job.child, job.childMethod, job.human, job.bones,
        job.items[1].klass,job.items[2].klass};
    if (!InitializeRigProbe(runtime, rigCalls, report)) return false;
    dispatchMessage = RegisterWindowMessageW(L"SSMT.Poser.Readonly.NativeProbe.V1");
    HMODULE module = nullptr;
    if (!dispatchMessage || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&Dispatch), &module)) return false;
    const auto marker = directory / L"scene_ready.flag";
    report << "phase=WAITING_PLAYABLE_SCENE; trigger=foreground_F8_or_scene_ready.flag\n";
    report.flush();
    const auto sceneDeadline = GetTickCount64() + 15 * 60 * 1000;
    bool confirmed = false;
    while (GetTickCount64() < sceneDeadline && WaitForSingleObject(stop, 250) != WAIT_OBJECT_0) {
        DWORD foreground = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &foreground);
        if (GetFileAttributesW(marker.c_str()) != INVALID_FILE_ATTRIBUTES ||
            (foreground == GetCurrentProcessId() && (GetAsyncKeyState(VK_F8) & 0x8000))) { confirmed = true; break; }
    }
    if (!confirmed) { report << "blocker=scene entry not confirmed or cancelled\n"; return false; }
    if (GetFileAttributesW((directory/L"motion_mode.flag").c_str())!=INVALID_FILE_ATTRIBUTES) {
        ConfigureMotionMode(); report << "write_experiment=DYNAMIC_VMD_FK_USER_REQUESTED\n";
    }
    if (GetFileAttributesW((directory/L"arm_test.flag").c_str())!=INVALID_FILE_ATTRIBUTES) {
        ConfigureArmExperiment(); report << "write_experiment=LEFT_UPPERARM_60_DEGREES_TWO_CYCLES_USER_REQUESTED\n";
    }
    std::ifstream targetFile(directory/L"target_actor_name.txt",std::ios::binary);
    std::string targetName;
    if (targetFile) std::getline(targetFile,targetName);
    if (!targetName.empty() && targetName.size()<128) {
        const auto size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,targetName.data(),int(targetName.size()),nullptr,0);
        std::wstring actorName(size,L'\0');
        if (size && MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,targetName.data(),int(targetName.size()),actorName.data(),size)) SetRigWriteTarget(actorName);
        report << "requested_actor_name=" << targetName << '\n';
    }
    report << "scene_entry=USER_CONFIRMED; character_writes=NONE\n";
    for (unsigned attempt = 0; attempt < 120; ++attempt) {
        report << "\n[scene snapshot " << attempt + 1 << "] tick=" << GetTickCount64() << '\n';
        // 清空上次历史观测，只保留已经核验的 native class/descriptor 身份。
        for (auto& item : job.items) {
            const auto klass = item.klass, descriptor = item.descriptor;
            const auto id = item.metadataId;
            item = Item{}; item.klass = klass; item.descriptor = descriptor; item.metadataId = id;
        }
        job.complete = false; job.fault = 0; job.step = 0; job.runtimeThread = 0;
        if (WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) return false;
        if (Read(job.context.mainThreadSlot, job.mainThread) && job.mainThread) {
            Window window{job.mainThread, nullptr};
            EnumWindows(SelectWindow, reinterpret_cast<LPARAM>(&window));
            if (window.hwnd) {
                auto hook = SetWindowsHookExW(WH_GETMESSAGE, Dispatch, module, job.mainThread);
                if (!hook) { report << "dispatch_hook_error=" << GetLastError() << '\n'; return false; }
                state.store(1, std::memory_order_release);
                if (!PostMessageW(window.hwnd, dispatchMessage, reinterpret_cast<WPARAM>(&job), 0)) state.store(0, std::memory_order_release);
                const auto deadline = GetTickCount64() + 30000;
                while (state.load(std::memory_order_acquire) != 3 && GetTickCount64() < deadline && WaitForSingleObject(stop, 10) != WAIT_OBJECT_0) {}
                unsigned pending = 1;
                state.compare_exchange_strong(pending, 0, std::memory_order_acq_rel);
                // 执行中的任务必须退出才能卸钩/返回，避免 shutdown 释放在用 DLL。
                while (state.load(std::memory_order_acquire) == 2) Sleep(1);
                const auto removed = UnhookWindowsHookEx(hook);
                const auto removeError = removed ? 0 : GetLastError();
                while (active.load(std::memory_order_acquire)) Sleep(1);
                if (!removed) {
                    // 无法确认卸钩时保留代码映射，避免宿主卸载后 Windows 调用悬空 callback。
                    HMODULE pinned = nullptr;
                    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                        reinterpret_cast<LPCWSTR>(&Dispatch), &pinned);
                    state.store(0, std::memory_order_release);
                    report << "dispatch_unhook=FAILED error=" << removeError << " module_pinned=" << bool(pinned) << '\n';
                    return false;
                }
                report << "dispatch_unhook=OK; callbacks_in_flight=0\n";
                report << "dispatch_state=" << state.load() << " actual_tid=" << job.actualThread << " Unity_main_tid=" << job.mainThread
                    << " step=" << job.step << " SEH=0x" << std::hex << job.fault << std::dec << '\n';
                Address(report, "borrowed_IL2CPP_thread", job.runtimeThread);
                report << "thread_attach=NOT_NEEDED_ALREADY_ATTACHED; thread_detach=NOT_OWNED_NOT_CALLED\n";
                bool success = false;
                for (size_t i = 1; i < job.items.size(); ++i) {
                    const auto& item = job.items[i];
                    report << "[LIVE] UnityEngine." << names[i] << " typeId=" << item.metadataId << '\n';
                    Address(report, "class", item.klass); Address(report, "type_descriptor", item.descriptor);
                    Address(report, "System.Type", item.reflection); Address(report, "System.Type_repeat", item.repeat);
                    Address(report, "reflection_type_descriptor", item.reflectionDescriptor);
                    Address(report, "array", item.array); Address(report, "array_class", item.arrayClass);
                    report << "array_length=" << item.length << " array_valid=" << item.arrayValid
                        << " skipped_nonexact_transform=" << item.skippedTransforms << '\n';
                    Address(report, "object", item.object); Address(report, "object_class", item.objectClass); Address(report, "native_object", item.nativeObject);
                    Address(report, "transform", item.transform); Address(report, "gameObject", item.gameObject); Address(report, "name_object", item.nameObject);
                    report << "childCount=" << item.childCount << " native_childCount=" << item.nativeChildCount
                        << " name=" << Utf8(item.name, std::clamp(item.nameLength, 0, 127))
                        << " GC_handles_verified=" << item.rooted << " GC_handles_freed=" << item.released
                        << " live_valid=" << item.liveValid << " GC_handles_released=" <<
                        std::all_of(std::begin(item.handles), std::end(item.handles), [](auto h) { return h == 0; }) << '\n';
                    success |= item.liveValid;
                    if (i == 2) report << "isHuman=" << item.isHuman << " isHuman_read=" << item.humanRead << '\n';
                    else {
                        Address(report, "rootBone", item.rootBone); Address(report, "bones_array", item.bonesArray);
                        Address(report, "sharedMesh", item.sharedMesh);
                        report << "bones_length=" << item.bonesLength << " native_bones_length=" << item.nativeBonesLength
                            << " bones_valid=" << item.bonesValid << " sharedMesh.name=" << Utf8(item.meshName, std::clamp(item.meshNameLength, 0, 127)) << '\n';
                    }
                }
                report.flush();
                if (!job.complete || job.fault || state.load() != 3) { ExportRigProbe(directory, report); return false; }
                if (success) {
                    const auto& item = job.items[1];
                    if (item.liveValid && item.bonesValid) {
                        std::ofstream bonesFile(directory / L"genshin_poser_bones.txt", std::ios::binary);
                        if (!bonesFile) { report << "bones_export=FILE_OPEN_FAILED\n"; return false; }
                        bonesFile << "SSMT Poser scene readonly bone snapshot; pid=" << GetCurrentProcessId()
                            << "\nobject_name=" << Utf8(item.name, item.nameLength) << "\nsharedMesh.name=" << Utf8(item.meshName, item.meshNameLength)
                            << "\nbones_length=" << item.bonesLength << "\nindex\tTransform\truntimeClass\tnativeTransform\tname\n";
                        for (size_t i = 0; i < item.bonesLength; ++i) {
                            const auto& bone = item.bones[i];
                            bonesFile << i << "\t0x" << std::hex << bone.object << "\t0x" << bone.klass << "\t0x" << bone.native << std::dec
                                << '\t' << (bone.object ? Utf8(bone.name, bone.nameLength) : "<null>") << '\n';
                        }
                        bonesFile.flush();
                        if (!bonesFile) { report << "bones_export=WRITE_FAILED\n"; return false; }
                        report << "bones_export=genshin_poser_bones.txt; slots=" << item.bonesLength << '\n';
                        if (!ExportRigProbe(directory, report)) return false;
                    }
                    return IsMotionMode()?RunMotionSession(runtime,stop,module,window,report,directory):RunBoneExperiment(runtime,stop,module,window,report,directory);
                }
            }
        }
        if (WaitForSingleObject(stop, 5000) == WAIT_OBJECT_0) return false;
    }
    report << "blocker=Unity main thread/window not ready\n";
    return false;
}
}

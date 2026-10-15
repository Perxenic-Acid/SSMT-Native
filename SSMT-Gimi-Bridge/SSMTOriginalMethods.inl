// Bridge 的各个 COM 对象可能分别来自 Runtime、系统和显卡组件。
// 转发必须使用当前对象保存的虚表，不能复用第一个设备或 Context 的方法。
struct SSMTOriginalBinding {
    void* instance = nullptr;
    void** current = nullptr;
    void** original = nullptr;
    void** clone = nullptr;
};

inline const SSMTOriginalBinding& ssmt_original_binding(void* instance)
{
    // 影子虚表保留至进程退出；以当前虚表区分同一地址上重建的对象。
    // 稳定渲染路径只读取线程缓存，不加锁、分配或查询模块。
    thread_local std::array<SSMTOriginalBinding, 8> cache{};
    thread_local std::size_t cursor = 0;
    void** current = *reinterpret_cast<void***>(instance);
    for (const auto& entry : cache) {
        if (entry.instance == instance && entry.current == current)
            return entry;
    }
    void** original = current;
    void** clone = nullptr;
    {
        std::lock_guard lock(g_vtable_mutex);
        const auto found = g_original_vtables.find(instance);
        if (found != g_original_vtables.end()) {
            original = found->second;
            clone = g_cloned_vtables.at(instance);
        }
    }
    auto& entry = cache[cursor++ % cache.size()];
    entry = {instance, current, original, clone};
    return entry;
}

template <typename Function>
Function ssmt_original_method(void* instance, std::size_t index)
{
    return reinterpret_cast<Function>(ssmt_original_binding(instance).original[index]);
}

ULONG STDMETHODCALLTYPE ssmt_release(IUnknown* instance)
{
    const auto binding = ssmt_original_binding(instance);
    const auto release = reinterpret_cast<ULONG (STDMETHODCALLTYPE*)(IUnknown*)>(binding.original[2]);
    const ULONG references = release(instance);
    if (references == 0) {
        // Release 后对象可能已销毁，不能再解引用 instance。仅删除同一代记录，
        // 避免另一线程已在该地址登记新对象时误删它。虚表保留以避免缓存 ABA。
        std::lock_guard lock(g_vtable_mutex);
        const auto found = g_cloned_vtables.find(instance);
        if (found != g_cloned_vtables.end() && found->second == binding.clone) {
            g_original_vtables.erase(instance);
            g_cloned_vtables.erase(found);
        }
    }
    return references;
}

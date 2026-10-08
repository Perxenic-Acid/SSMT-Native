#include "GenshinNativeRuntime.h"
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace poser {
namespace {
bool Cancelled(HANDLE stop) { return stop && WaitForSingleObject(stop, 0) == WAIT_OBJECT_0; }
bool Copy(uintptr_t address, void* out, size_t size) {
    SIZE_T copied = 0;
    return address && size <= 64 * 1024 * 1024 && address <= UINTPTR_MAX - size &&
        ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), out, size, &copied) && copied == size;
}
template<class T> T Read(uintptr_t address) {
    T result{};
    if (!Copy(address, &result, sizeof(result))) throw std::runtime_error("unreadable native runtime address");
    return result;
}
std::string Text(uintptr_t address) {
    std::string result;
    for (size_t i = 0; i < 256; ++i) {
        const char ch = Read<char>(address + i);
        if (!ch) return result;
        result += ch;
    }
    throw std::runtime_error("unterminated native runtime text");
}
bool Executable(uintptr_t address) {
    MEMORY_BASIC_INFORMATION m{};
    return address && VirtualQuery(reinterpret_cast<void*>(address), &m, sizeof(m)) && m.State == MEM_COMMIT &&
        !(m.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
        (m.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
}
std::string Hash(const std::filesystem::path& path, HANDLE stop) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("SHA256 algorithm unavailable");
    struct Cleanup {
        BCRYPT_ALG_HANDLE& a; BCRYPT_HASH_HANDLE& h;
        ~Cleanup() { if (h) BCryptDestroyHash(h); if (a) BCryptCloseAlgorithmProvider(a, 0); }
    } cleanup{algorithm, hash};
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0)
        throw std::runtime_error("SHA256 initialization failed");
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("sample file unavailable");
    std::vector<uint8_t> block(1024 * 1024);
    while (file) {
        if (Cancelled(stop)) return {};
        file.read(reinterpret_cast<char*>(block.data()), block.size());
        const auto count = file.gcount();
        if (count && BCryptHashData(hash, block.data(), static_cast<ULONG>(count), 0) < 0)
            throw std::runtime_error("SHA256 update failed");
    }
    if (!file.eof()) throw std::runtime_error("sample file read failed");
    std::array<uint8_t, 32> digest{};
    if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
        throw std::runtime_error("SHA256 finish failed");
    std::ostringstream text;
    for (auto value : digest) text << std::hex << std::setw(2) << std::setfill('0') << unsigned(value);
    return text.str();
}
uintptr_t Rip(uintptr_t address) {
    return native_detail::RelativeTarget(address, Read<int32_t>(address + 3), 7);
}
uintptr_t Branch(uintptr_t address) {
    const auto opcode = Read<uint8_t>(address);
    if (opcode != 0xE8 && opcode != 0xE9) throw std::runtime_error("expected direct rel32 branch");
    return native_detail::RelativeTarget(address, Read<int32_t>(address + 1), 5);
}
uintptr_t Unique(const std::vector<uint8_t>& bytes, uintptr_t base, const char* pattern) {
    const auto matches = native_detail::FindPattern(bytes, pattern);
    if (matches.size() != 1) {
        std::ostringstream error;
        error << "native semantic anchor at 0x" << std::hex << base << std::dec
            << " matches=" << matches.size() << " pattern=" << pattern;
        throw std::runtime_error(error.str());
    }
    return base + matches[0];
}
std::vector<uint8_t> Bytes(uintptr_t address, size_t count) {
    std::vector<uint8_t> bytes(count);
    if (!Copy(address, bytes.data(), bytes.size())) throw std::runtime_error("native code/data read failed");
    return bytes;
}
// 全部字段与变换来自本轮保存的消费者反汇编；只允许双哈希匹配的样本使用此 profile。
std::string Decode(uint32_t token, uintptr_t pool) {
    if (token == UINT32_MAX || !(token >> 24)) return {};
    const uint32_t length = token >> 24, offset = token & 0xFFFFFF;
    auto bytes = Bytes(pool + offset, (length + 7) & ~size_t(7));
    uint64_t key = 0x30EE5B43130FE0CDull ^ (0x6D8C4AAB00FE8E27ull + 0xB33E40427D5668C0ull * offset);
    for (size_t i = 0; i < bytes.size(); i += 8) {
        uint64_t value = 0;
        std::memcpy(&value, bytes.data() + i, 8);
        value ^= key;
        std::memcpy(bytes.data() + i, &value, 8);
        key += 0x0889EEEA326AFB36ull;
    }
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        reinterpret_cast<char*>(bytes.data()), length, nullptr, 0)) return {};
    return std::string(reinterpret_cast<char*>(bytes.data()), length);
}
uint32_t MethodKey(uint32_t id) {
    uint32_t t = (id * 0x4E94) ^ 0x62D8B7B8;
    return (t * 0x7572748A + 0x1F4D9EEA) ^ 0x6E8931D6;
}
void Address(std::ostream& out, const char* key, uintptr_t value) {
    out << key << "=0x" << std::hex << value << std::dec << '\n';
}
}

namespace native_detail {
std::vector<size_t> FindPattern(const std::vector<uint8_t>& bytes, const char* pattern) {
    std::istringstream input(pattern);
    std::vector<int> needle;
    std::string token;
    while (input >> token) needle.push_back(token == "??" ? -1 : std::stoi(token, nullptr, 16));
    if (needle.empty() || needle[0] < 0) throw std::invalid_argument("pattern requires a literal first byte");
    std::vector<size_t> result;
    auto cursor = bytes.begin();
    while (cursor != bytes.end()) {
        cursor = std::find(cursor, bytes.end(), static_cast<uint8_t>(needle[0]));
        if (cursor == bytes.end()) break;
        const auto index = static_cast<size_t>(cursor - bytes.begin());
        if (needle.size() > bytes.size() - index) break;
        bool match = true;
        for (size_t i = 1; i < needle.size(); ++i)
            if (needle[i] >= 0 && needle[i] != bytes[index + i]) { match = false; break; }
        if (match) result.push_back(index);
        ++cursor;
    }
    return result;
}
uintptr_t RelativeTarget(uintptr_t instruction, int32_t displacement, size_t length) {
    const auto next = instruction + length;
    if (next < instruction || (displacement < 0 && next < uint64_t(-int64_t(displacement))) ||
        (displacement >= 0 && next > UINTPTR_MAX - uint32_t(displacement)))
        throw std::runtime_error("relative address overflow");
    return displacement < 0 ? next - uintptr_t(-int64_t(displacement)) : next + uint32_t(displacement);
}
bool ValidateClass(uintptr_t address, uintptr_t record, const std::string& name, uint16_t count) {
    if (!address) return false;
    try {
        return Read<uintptr_t>(address + 0x60) == record &&
            Text(Read<uintptr_t>(address + 0x28)) == name && Read<uint16_t>(address + 0xC0) == count;
    } catch (...) { return false; }
}
bool ValidateMethod(uintptr_t descriptor, uintptr_t klass, uint32_t id, uint8_t parameters, uintptr_t entry) {
    if (!descriptor) return false;
    try {
        return Read<uintptr_t>(descriptor) == klass && Read<uintptr_t>(descriptor + 8) == entry &&
            Read<uint32_t>(descriptor + 0x10) == id && Read<uint8_t>(descriptor + 0x2E) == parameters;
    } catch (...) { return false; }
}
}

NativeResult GenshinNativeRuntime::Initialize(HMODULE image, HANDLE stop, std::ostream& report) {
    *this = GenshinNativeRuntime{};
    try {
        if (Cancelled(stop)) return NativeResult::Cancelled;
        wchar_t path[32768]{};
        const DWORD count = GetModuleFileNameW(image, path, static_cast<DWORD>(std::size(path)));
        if (!count || count == std::size(path)) throw std::runtime_error("image path unavailable");
        const std::filesystem::path exe(path);
        const auto metadata = exe.parent_path() / (exe.stem().wstring() + L"_Data") /
            L"Managed" / L"Metadata" / L"global-metadata.dat";
        const auto exeHash = Hash(exe, stop), metadataHash = Hash(metadata, stop);
        report << "[Genshin native backend] profile=GI71_CN_READONLY_V1\nexe_sha256=" << exeHash
            << "\nmetadata_sha256=" << metadataHash << '\n';
        if (Cancelled(stop)) return NativeResult::Cancelled;
        if (exeHash != "7f89938da606c1281659607d464702cddb9a09a7d4320a196630f60a811ec38e" ||
            metadataHash != "469eccd43aa48fe7a2df4e1caf66fa1268a17f5a4a03fa209e1427e467cb0161") {
            report << "native=UNSUPPORTED_SAMPLE; no RVA fallback\n";
            return NativeResult::Unsupported;
        }
        image_ = reinterpret_cast<uintptr_t>(image);
        const auto dos = Read<IMAGE_DOS_HEADER>(image_);
        if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 1024 * 1024)
            throw std::runtime_error("invalid DOS header");
        const auto nt = Read<IMAGE_NT_HEADERS64>(image_ + dos.e_lfanew);
        if (nt.Signature != IMAGE_NT_SIGNATURE || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt.FileHeader.NumberOfSections > 96)
            throw std::runtime_error("invalid PE header");
        imageSize_ = nt.OptionalHeader.SizeOfImage;
        uintptr_t methodAnchor = 0, registration = 0;
        size_t methodMatches = 0, registrationMatches = 0;
        std::vector<uintptr_t> registrationCandidates;
        const char* methodPattern =
            "48 8B 05 ?? ?? ?? ?? 48 63 D9 48 8B 04 D8 48 85 C0 0F 85 ?? ?? ?? ?? 89 CE "
            "48 8B 05 ?? ?? ?? ?? 48 63 80 E0 01 00 00 48 35 FB 80 E4 2E 48 03 05 ?? ?? ?? ?? "
            "89 C9 69 D6 94 4E 00 00 81 F2 B8 B7 D8 62 69 D2 8A 74 72 75 81 C2 EA 9E 4D 1F "
            "81 F2 D6 31 89 6E 69 D2 8B 9C FC C6 48 8D 3C 89 48 8D 3C BF 48 01 CF "
            "B9 9C 72 94 03 33 4C 38 0E 01 D1 E8 ?? ?? ?? ?? 48 89 C7 66 83 B8 C0 00 00 00 00";
        const char* registrationPattern =
            "48 8D 05 ?? ?? ?? ?? 48 89 05 ?? ?? ?? ?? 48 8D 05 ?? ?? ?? ?? 48 89 05 ?? ?? ?? ?? "
            "48 8D 05 ?? ?? ?? ?? 48 89 05 ?? ?? ?? ?? 48 8D 05 ?? ?? ?? ?? 48 89 05 ?? ?? ?? ?? "
            "48 8D 05 ?? ?? ?? ?? 48 89 05 ?? ?? ?? ?? C3";
        for (unsigned i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
            const auto section = Read<IMAGE_SECTION_HEADER>(image_ + dos.e_lfanew + 24 +
                nt.FileHeader.SizeOfOptionalHeader + i * sizeof(IMAGE_SECTION_HEADER));
            if (!(section.Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
            if (section.VirtualAddress > imageSize_ || section.Misc.VirtualSize > imageSize_ - section.VirtualAddress)
                throw std::runtime_error("section outside image");
            // 块间重叠保证跨边界签名不遗漏；每个候选起点只计一次。
            constexpr size_t Block = 1024 * 1024, Overlap = 256;
            for (size_t offset = 0; offset < section.Misc.VirtualSize; offset += Block) {
                if (Cancelled(stop)) return NativeResult::Cancelled;
                const auto size = std::min(Block + Overlap, size_t(section.Misc.VirtualSize) - offset);
                const auto base = image_ + section.VirtualAddress + offset;
                const auto bytes = Bytes(base, size);
                for (auto index : native_detail::FindPattern(bytes, methodPattern)) {
                    if (index >= Block) continue;
                    ++methodMatches; methodAnchor = base + index;
                }
                for (auto index : native_detail::FindPattern(bytes, registrationPattern)) {
                    if (index >= Block) continue;
                    registrationCandidates.push_back(base + index);
                }
            }
        }
        report << "method_materializer_matches=" << methodMatches
            << "\nregistration_pattern_candidates=" << registrationCandidates.size() << '\n';
        if (methodMatches != 1) throw std::runtime_error("method anchor not unique");
        headerSlot_ = Rip(methodAnchor + 25);
        // 五组 lea/store 只是候选。必须与独立 method 消费者指向同一个 MHY header。
        for (auto candidate : registrationCandidates) {
            const auto headerObject = Rip(candidate + 56);
            if (Rip(candidate + 63) != headerSlot_ || headerObject < image_ ||
                headerObject > image_ + imageSize_ - 4) continue;
            if (Read<uint32_t>(headerObject) != 0x0059484D) continue;
            registration = candidate; ++registrationMatches;
        }
        report << "registration_semantic_matches=" << registrationMatches << '\n';
        if (registrationMatches != 1) throw std::runtime_error("registration relationship not unique");
        Address(report, "image", image_);
        Address(report, "method_materializer_anchor", methodAnchor);
        Address(report, "registration_setup", registration);
        methodCacheSlot_ = Rip(methodAnchor);
        bodySlot_ = Rip(methodAnchor + 45);
        codeSlot_ = Rip(registration + 7);
        if (Rip(registration + 63) != headerSlot_ || Rip(registration + 56) < image_ ||
            Rip(registration + 56) >= image_ + imageSize_)
            throw std::runtime_error("registration header relationship failed");
        const auto classEntry = Branch(methodAnchor + 112);
        const auto classBody = Branch(classEntry);
        Address(report, "class_materializer_entry", classEntry);
        Address(report, "class_materializer_body", classBody);
        if (!Executable(classEntry) || !Executable(classBody) || classBody < image_ || classBody >= image_ + imageSize_)
            throw std::runtime_error("class materializer branch failed");
        const auto classBytes = Bytes(classBody, 4096);
        const auto classRead = Unique(classBytes, classBody,
            "89 CF 48 8B 05 ?? ?? ?? ?? 66 45 0F AC EE B7 66 41 C1 C6 6B 4C 63 F1 F8 4A 8B 04 F0 48 85 C0");
        const auto classWrite = Unique(classBytes, classBody, "48 8B 05 ?? ?? ?? ?? 4E 89 24 F0");
        classCacheSlot_ = Rip(classRead + 2);
        if (classCacheSlot_ != Rip(classWrite)) throw std::runtime_error("class cache read/write disagreement");
        Unique(classBytes, classBody, "4D 89 7C 24 60");
        Unique(classBytes, classBody, "49 89 44 24 28");
        Unique(classBytes, classBody, "41 0F B7 4F 3C F8 81 F1 11 88 00 00");
        const auto methodBytes = Bytes(methodAnchor, 224);
        const auto cacheWrite = Unique(methodBytes, methodAnchor, "48 8B 0D ?? ?? ?? ?? 48 89 04 D9");
        if (Rip(cacheWrite) != methodCacheSlot_) throw std::runtime_error("method cache read/write disagreement");
        const auto methodSetup = Branch(methodAnchor + 166);
        const auto setup = Bytes(methodSetup, 1280);
        Unique(setup, methodSetup, "4C 89 33 89 7B 10");
        Unique(setup, methodSetup, "88 53 2E");
        const auto codeRead = Unique(setup, methodSetup, "48 8B 0D ?? ?? ?? ?? 48 89 4C 24 20");
        if (Rip(codeRead) != codeSlot_) throw std::runtime_error("method/code registration disagreement");
        Address(report, "class_cache_slot", classCacheSlot_);
        Address(report, "method_cache_slot", methodCacheSlot_);
        Address(report, "header_slot", headerSlot_);
        Address(report, "metadata_body_slot", bodySlot_);
        Address(report, "code_registration_slot", codeSlot_);
        report << "native_patterns=UNIQUE_AND_CROSSCHECKED; invocations=NONE\n";
        initialized_ = true;
        return NativeResult::Ready;
    } catch (const std::exception& error) {
        report << "native_initialize=FAILED reason=" << error.what() << '\n';
        return NativeResult::Failed;
    }
}

NativeResult GenshinNativeRuntime::Snapshot(HANDLE stop, std::ostream& report) {
    classes_.clear();
    try {
        if (Cancelled(stop)) return NativeResult::Cancelled;
        if (!initialized_) throw std::runtime_error("backend not initialized");
        header_ = Read<uintptr_t>(headerSlot_); body_ = Read<uintptr_t>(bodySlot_);
        classCache_ = Read<uintptr_t>(classCacheSlot_); methodCache_ = Read<uintptr_t>(methodCacheSlot_);
        const auto code = Read<uintptr_t>(codeSlot_);
        Address(report, "runtime_header", header_); Address(report, "metadata_body", body_);
        Address(report, "class_cache", classCache_); Address(report, "method_cache", methodCache_);
        Address(report, "code_registration", code);
        if (!header_ || !body_ || !classCache_ || !methodCache_ || !code) return NativeResult::Waiting;
        if (Read<uint32_t>(header_) != 0x0059484D) throw std::runtime_error("runtime metadata header marker mismatch");
        const auto typesSize = Read<uint32_t>(header_ + 0x60) ^ 0x15335474;
        const auto methodsSize = Read<uint32_t>(header_ + 0x134) + uint32_t(0x8EEFDB56);
        if (typesSize % 70 || methodsSize % 26 || !typesSize || !methodsSize ||
            typesSize / 70 > 500000 || methodsSize / 26 > 2000000)
            throw std::runtime_error("invalid metadata counts");
        typeCount_ = typesSize / 70; methodCount_ = methodsSize / 26;
        typeTable_ = body_ + (Read<uint32_t>(header_ + 0xDC) ^ 0x5278CA3B);
        methodTable_ = body_ + (Read<uint32_t>(header_ + 0x1E0) ^ 0x2EE480FB);
        strings_ = body_ + (Read<uint32_t>(header_ + 0x150) + uint32_t(0xC8542DD2));
        codeTable_ = Read<uintptr_t>(code + 0x28);
        report << "metadata_type_count=" << typeCount_ << "\nmetadata_method_count=" << methodCount_ << '\n';
        Address(report, "type_records", typeTable_); Address(report, "method_records", methodTable_);
        Address(report, "string_pool", strings_); Address(report, "method_pointer_table", codeTable_);
        const auto records = Bytes(typeTable_, typesSize);
        const std::array targets = {"Component", "GameObject", "SkinnedMeshRenderer", "Object", "Transform", "Animator"};
        size_t materialized = 0;
        for (uint32_t id = 0; id < typeCount_; ++id) {
            if ((id & 255) == 0 && Cancelled(stop)) return NativeResult::Cancelled;
            uint32_t nameToken = 0;
            std::memcpy(&nameToken, records.data() + id * 70 + 0x14, 4);
            nameToken += 0xE811F779;
            const auto length = nameToken >> 24;
            if (std::none_of(targets.begin(), targets.end(), [length](auto name) { return std::strlen(name) == length; })) continue;
            const auto name = Decode(nameToken, strings_);
            if (std::find(targets.begin(), targets.end(), name) == targets.end()) continue;
            const auto record = typeTable_ + id * 70;
            const auto namespaze = Decode(Read<uint32_t>(record + 0x18) ^ 0x666769B5, strings_);
            report << "[TYPE_IDENTITY] " << namespaze << '.' << name << " metadataTypeId=" << id << '\n';
            if (namespaze != "UnityEngine") continue;
            NativeClass klass;
            klass.metadataId = id; klass.name = name; klass.namespaze = namespaze; klass.metadataRecord = record;
            klass.methodStart = ~(Read<uint32_t>(record + 8) ^ 0xFB8750A6);
            klass.methodCount = Read<uint16_t>(record + 0x3C) ^ 0x8811;
            if (klass.methodStart > methodCount_ || klass.methodCount > methodCount_ - klass.methodStart)
                throw std::runtime_error("class method range invalid");
            klass.address = Read<uintptr_t>(classCache_ + id * 8);
            if (klass.address && !native_detail::ValidateClass(klass.address, record, name, klass.methodCount))
                throw std::runtime_error("class/cache/metadata identity disagreement");
            report << "[TYPE] " << namespaze << '.' << name << " metadataTypeId=" << id
                << " methodStart=" << klass.methodStart << " methodCount=" << klass.methodCount << '\n';
            Address(report, "  metadataRecord", record); Address(report, "  runtimeClass", klass.address);
            report << "  identity=" << (klass.address ? "CACHE_RECORD_NAME_COUNT_CONFIRMED" : "NOT_MATERIALIZED") << '\n';
            if (klass.address) ++materialized;
            classes_.push_back(std::move(klass));
        }
        if (classes_.size() != targets.size()) throw std::runtime_error("Unity type set missing/ambiguous");
        for (const auto& klass : classes_) {
            for (auto name : {"get_childCount", "GetChild", "get_transform", "get_gameObject", "get_name",
                "get_bones", "get_rootBone", "get_sharedMesh", "get_isHuman", "GetBoneTransform", "FindObjectsOfType"}) {
                for (uint8_t parameters : {uint8_t(0), uint8_t(1)}) {
                    NativeMethod method;
                    if (!FindMethod(klass, name, parameters, method)) continue;
                    report << "[METHOD] " << klass.namespaze << '.' << klass.name << '.' << method.name
                        << " metadataMethodId=" << method.metadataId << " parameterCount=" << unsigned(method.parameterCount) << '\n';
                    Address(report, "  nativeDescriptor", method.descriptor);
                    Address(report, "  entry", method.entry); Address(report, "  jumpTarget", method.target);
                    report << "  identity=" << (method.descriptor ? "CLASS_ID_COUNT_ENTRY_CONFIRMED" : "DESCRIPTOR_NOT_MATERIALIZED")
                        << " invoked=NO; callable_abi=" << (method.entry ? "UNVERIFIED" : "NULL_ENTRY_NOT_CALLABLE") << '\n';
                }
            }
        }
        report << "materialized_target_classes=" << materialized
            << "\nSystem.Type=NOT_RESOLVED\nlive_Unity_object=NOT_ENUMERATED\n"
            << "worker_operations=ReadProcessMemory_only; Unity_thread_context=NOT_ESTABLISHED\n";
        return materialized == targets.size() ? NativeResult::Ready : NativeResult::Waiting;
    } catch (const std::exception& error) {
        classes_.clear();
        report << "native_snapshot=FAILED reason=" << error.what() << '\n';
        return NativeResult::Failed;
    }
}

bool GenshinNativeRuntime::FindClass(const char* namespaze, const char* name, NativeClass& result) const {
    const NativeClass* selected = nullptr;
    for (const auto& klass : classes_) {
        if (klass.namespaze != namespaze || klass.name != name) continue;
        if (selected || !klass.address) return false;
        selected = &klass;
    }
    if (!selected || !native_detail::ValidateClass(selected->address, selected->metadataRecord, selected->name, selected->methodCount)) return false;
    result = *selected;
    return true;
}

bool GenshinNativeRuntime::DescribeClass(uintptr_t address, NativeClass& result) const {
    try {
        if (!initialized_ || !address || !typeTable_ || !classCache_) return false;
        const auto record = Read<uintptr_t>(address + 0x60);
        if (record < typeTable_ || (record - typeTable_) % 70) return false;
        const auto id = (record - typeTable_) / 70;
        if (id >= typeCount_ || Read<uintptr_t>(classCache_ + id * 8) != address) return false;
        NativeClass found;
        found.address = address; found.metadataRecord = record; found.metadataId = static_cast<uint32_t>(id);
        found.name = Decode(Read<uint32_t>(record + 0x14) + uint32_t(0xE811F779), strings_);
        found.namespaze = Decode(Read<uint32_t>(record + 0x18) ^ 0x666769B5, strings_);
        found.methodStart = ~(Read<uint32_t>(record + 8) ^ 0xFB8750A6);
        found.methodCount = Read<uint16_t>(record + 0x3C) ^ 0x8811;
        if (!native_detail::ValidateClass(address, record, found.name, found.methodCount)) return false;
        result = std::move(found);
        return true;
    } catch (...) { return false; }
}

bool GenshinNativeRuntime::ResolveCallContext(HANDLE stop, NativeCallContext& result, std::ostream& report) const {
    try {
        if (!initialized_ || !typeTable_) throw std::runtime_error("runtime identity not ready");
        const std::array patterns = {
            "55 41 57 41 56 56 57 53 48 83 EC 48 48 8D 6C 24 40 48 C7 45 00 FE FF FF FF 48 89 4D F0 83 3D ?? ?? ?? ?? 00 4C 8D 35 ?? ?? ?? ?? 48 8D 35 ?? ?? ?? ?? 48 89 F7 49 0F 44 FE",
            "8B 3D ?? ?? ?? ?? 48 8B DA 48 8B F1 E8 ?? ?? ?? ?? 3B C7 74 12 E8 ?? ?? ?? ?? 48 85 C0 75 08 48 8B CB E8 ?? ?? ?? ??",
            "55 48 83 EC 30 48 8D 6C 24 30 48 C7 45 F8 FE FF FF FF 49 89 C8 0F B6 D2 48 83 CA 02 48 8D 04 52 48 8D 0D ?? ?? ?? ?? 48 8D 0C C1 E8 ?? ?? ?? ??",
            "89 C8 C1 E8 1A 48 C1 E0 10 48 8D 15 ?? ?? ?? ?? 48 01 C2 89 C8 C1 E8 0D 25 FF 1F 00 00 48 8B 04 C2 81 E1 FF 1F 00 00 48 8B 04 C8 C3 CC CC CC CC 55 56 48 83 EC 28",
            "C3 CC CC CC CC 55 56 48 83 EC 28 48 8D 6C 24 20 48 C7 45 00 FE FF FF FF 89 CE 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 85 C0"};
        std::array<std::vector<uintptr_t>, 5> matches;
        const auto dos = Read<IMAGE_DOS_HEADER>(image_);
        const auto nt = Read<IMAGE_NT_HEADERS64>(image_ + dos.e_lfanew);
        for (unsigned i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
            const auto section = Read<IMAGE_SECTION_HEADER>(image_ + dos.e_lfanew + 24 +
                nt.FileHeader.SizeOfOptionalHeader + i * sizeof(IMAGE_SECTION_HEADER));
            if (!(section.Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
            constexpr size_t Block = 1024 * 1024;
            for (size_t offset = 0; offset < section.Misc.VirtualSize; offset += Block) {
                if (Cancelled(stop)) return false;
                const auto base = image_ + section.VirtualAddress + offset;
                const auto bytes = Bytes(base, std::min(Block + 256, size_t(section.Misc.VirtualSize) - offset));
                for (size_t p = 0; p < patterns.size(); ++p)
                    for (auto index : native_detail::FindPattern(bytes, patterns[p]))
                        if (index < Block) matches[p].push_back(base + index);
            }
        }
        for (size_t i = 0; i < matches.size(); ++i) {
            report << "call_context_anchor_" << i << "_matches=" << matches[i].size() << '\n';
            if (matches[i].size() != 1) throw std::runtime_error("typed call anchor missing/ambiguous");
        }
        NativeCallContext found;
        found.reflectionType = matches[0][0];
        const auto factory = Bytes(found.reflectionType, 256);
        const auto alloc = Unique(factory, found.reflectionType, "48 8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 49 89 C7 48 8B 45 F0 49 89 47 10");
        found.reflectionClassSlot = Rip(alloc);
        const auto cache = Unique(factory, found.reflectionType, "48 8B 0D ?? ?? ?? ?? 48 8D 55 F0 4C 8D 45 F8 E8 ?? ?? ?? ?? 88 C3");
        found.reflectionCacheSlot = Rip(cache);
        const auto thread = matches[1][0];
        found.mainThreadSlot = native_detail::RelativeTarget(thread, Read<int32_t>(thread + 2), 6);
        found.threadCurrent = Branch(thread + 21);
        found.threadAttach = Branch(thread + 34);
        Unique(Bytes(found.threadCurrent, 48), found.threadCurrent,
            "56 48 83 EC 20 48 8B 05 ?? ?? ?? ?? 8B 08 FF 15 ?? ?? ?? ?? 48 89 C6 48 85 C0");
        const auto attachBody = Branch(found.threadAttach);
        const auto attach = Bytes(attachBody, 96);
        const auto attachTls = Unique(attach, attachBody, "48 8B 05 ?? ?? ?? ?? 8B 08 FF 15 ?? ?? ?? ?? 48 89 C7 48 85 C0");
        if (Rip(found.threadCurrent + 5) != Rip(attachTls)) throw std::runtime_error("thread TLS relationship disagreement");
        found.handleNew = matches[2][0]; found.handleTarget = matches[3][0]; found.handleFree = matches[4][0] + 5;
        if (found.handleFree != found.handleTarget + 48) throw std::runtime_error("GC handle API boundary disagreement");
        const auto freeBytes = Bytes(found.handleFree, 128);
        const auto freeTable = Unique(freeBytes, found.handleFree, "48 8D 15 ?? ?? ?? ?? 48 01 C2 89 F0 C1 E8 0D");
        if (native_detail::RelativeTarget(found.handleTarget + 9, Read<int32_t>(found.handleTarget + 12), 7) != Rip(freeTable))
            throw std::runtime_error("GC handle read/free table disagreement");
        const auto allocator = Branch(found.handleNew + 43);
        const auto allocatorBytes = Bytes(allocator, 128);
        const auto handleTable = Unique(allocatorBytes, allocator, "48 8D 1D ?? ?? ?? ?? 48 01 C3");
        if (Rip(handleTable) != Rip(freeTable) || Rip(found.handleNew + 32) + 0x60 != Rip(freeTable))
            throw std::runtime_error("GC handle allocate/read/free relationship disagreement");
        for (auto address : {found.reflectionType, found.threadCurrent, found.threadAttach,
            found.handleNew, found.handleTarget, found.handleFree})
            if (!Executable(address) || address < image_ || address >= image_ + imageSize_)
                throw std::runtime_error("typed call outside executable image");
        Address(report, "reflection_type_factory", found.reflectionType);
        Address(report, "reflection_class_slot", found.reflectionClassSlot);
        Address(report, "reflection_cache_slot", found.reflectionCacheSlot);
        Address(report, "thread_current", found.threadCurrent); Address(report, "thread_attach", found.threadAttach);
        Address(report, "Unity_main_thread_slot", found.mainThreadSlot);
        Address(report, "GC_handle_new", found.handleNew); Address(report, "GC_handle_target", found.handleTarget);
        Address(report, "GC_handle_free", found.handleFree);
        result = found;
        return true;
    } catch (const std::exception& error) {
        report << "call_context_resolve=FAILED reason=" << error.what() << '\n';
        return false;
    }
}
bool GenshinNativeRuntime::ResolveScriptLateUpdate(HANDLE stop,uintptr_t& result,std::ostream& report) const {
    result=0;
    try {
        if (!initialized_) throw std::runtime_error("sample profile not verified");
        // 只匹配当前双哈希 profile 的无参 void profiler wrapper，再核验 marker 与实际回调槽。
        const char* pattern="48 89 5C 24 18 57 48 83 EC 20 E8 ?? ?? ?? ?? 48 8B C8 48 8D 15 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8D 4C 24 38 8B F8 FF 15 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B C8 48 8B 10 FF 52 08 48 8D 4C 24 30 FF 15 ?? ?? ?? ?? 48 8B 5C 24 30 48 2B 5C 24 38 E8 ?? ?? ?? ?? 48 8B C8 4C 8B C3 8B D7 E8 ?? ?? ?? ?? 48 8B 5C 24 40 48 83 C4 20 5F C3";
        std::vector<uintptr_t> candidates,registrations,selected;
        const auto dos=Read<IMAGE_DOS_HEADER>(image_);
        const auto nt=Read<IMAGE_NT_HEADERS64>(image_+dos.e_lfanew);
        for (unsigned i=0;i<nt.FileHeader.NumberOfSections;++i) {
            const auto section=Read<IMAGE_SECTION_HEADER>(image_+dos.e_lfanew+24+nt.FileHeader.SizeOfOptionalHeader+i*sizeof(IMAGE_SECTION_HEADER));
            if (!(section.Characteristics&IMAGE_SCN_MEM_EXECUTE)) continue;
            constexpr size_t Block=1024*1024;
            for (size_t offset=0;offset<section.Misc.VirtualSize;offset+=Block) {
                if (Cancelled(stop)) return false;
                const auto base=image_+section.VirtualAddress+offset;
                const auto bytes=Bytes(base,std::min(Block+256,size_t(section.Misc.VirtualSize)-offset));
                for (auto index:native_detail::FindPattern(bytes,pattern)) if (index<Block) candidates.push_back(base+index);
                for (auto index:native_detail::FindPattern(bytes,"48 8D 05 ?? ?? ?? ?? 48 89 05 ?? ?? ?? ??"))
                    if (index<Block) registrations.push_back(base+index);
            }
        }
        for (auto candidate:candidates) {
            const auto marker=Rip(candidate+0x12);
            if (marker>=image_ && marker<image_+imageSize_-256 && Text(marker)=="ScriptRunBehaviourLateUpdate") selected.push_back(candidate);
        }
        report << "lateupdate_wrapper_candidates=" << candidates.size() << " marker_matches=" << selected.size() << '\n';
        if (selected.size()!=1) throw std::runtime_error("LateUpdate wrapper missing or ambiguous");
        unsigned slots=0;
        for (auto registration:registrations) {
            if (Rip(registration)!=selected[0]) continue;
            const auto slot=Rip(registration+7);
            if (slot>=image_ && slot<image_+imageSize_-8 && Read<uintptr_t>(slot)==selected[0]) { ++slots; Address(report,"lateupdate_registered_slot",slot); }
        }
        if (slots!=1 || !Executable(selected[0])) throw std::runtime_error("LateUpdate registration identity failed");
        result=selected[0]; Address(report,"lateupdate_callback",result);
        report << "lateupdate_ABI=void_no_arguments; registered_slots=1; timing=REQUIRES_LIVE_VALIDATION\n";
        return true;
    } catch (const std::exception& error) { report << "lateupdate_blocker=" << error.what() << '\n'; return false; }
}
bool GenshinNativeRuntime::FindMethod(const NativeClass& klass, const char* name, uint8_t parameters, NativeMethod& result) const {
    NativeMethod selected;
    size_t matches = 0;
    for (uint32_t id = klass.methodStart; id < klass.methodStart + klass.methodCount; ++id) {
        const auto record = methodTable_ + id * 26;
        const auto key = MethodKey(id);
        const auto count = uint8_t(Read<uint8_t>(record + 0x18) + 0x8C - uint8_t(key * 0x39036375));
        if (count != parameters) continue;
        const auto methodName = Decode((Read<uint32_t>(record + 4) ^ 0x0613ACE3) + key * 0xC6FC9C8B, strings_);
        if (methodName != name) continue;
        if (((Read<uint32_t>(record + 0xE) ^ 0x0394729C) + key * 0xC6FC9C8B) != klass.metadataId)
            throw std::runtime_error("method declaring type disagreement");
        ++matches;
        selected.metadataId = id; selected.parameterCount = count; selected.name = methodName;
        selected.entry = Read<uintptr_t>(codeTable_ + id * 8);
        if (selected.entry && !Executable(selected.entry)) throw std::runtime_error("method pointer not executable");
        selected.target = selected.entry;
        if (selected.entry && Read<uint8_t>(selected.entry) == 0xE9) {
            selected.target = Branch(selected.entry);
            if (!Executable(selected.target)) throw std::runtime_error("method branch not executable");
        }
        // 启动阶段 materializer 可并发填充缓存；class 尚未观测到时不拼接稍后出现的 descriptor。
        selected.descriptor = klass.address ? Read<uintptr_t>(methodCache_ + id * 8) : 0;
        if (!selected.descriptor && klass.address) {
            const auto methods = Read<uintptr_t>(klass.address + 0x58);
            if (methods) selected.descriptor = Read<uintptr_t>(methods + (id - klass.methodStart) * 8);
        }
        if (selected.descriptor && !native_detail::ValidateMethod(selected.descriptor, klass.address, id, count, selected.entry))
            throw std::runtime_error("method descriptor/class/id/count/code-table disagreement");
    }
    if (matches != 1) return false;
    result = std::move(selected);
    return true;
}
}

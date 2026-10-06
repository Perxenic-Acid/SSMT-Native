#pragma once

#include "PatternScanner.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace SSMT::Tweaks
{
    enum class ResolverKind { Direct, RelativeBranch };

    struct ResolvedSymbol
    {
        std::uintptr_t address = 0;
        ResolverKind kind = ResolverKind::Direct;
        std::size_t candidates = 0;
        std::string validation;

        explicit operator bool() const { return address != 0; }
    };

    using SymbolValidator = bool (*)(const PatternScanner &, std::uintptr_t);

    // 仅在候选经过局部结构验证且最终目标唯一时提交符号。
    ResolvedSymbol ResolveSymbol(
        const PatternScanner &scanner,
        std::string_view pattern,
        ResolverKind kind,
        SymbolValidator validator = nullptr);
}

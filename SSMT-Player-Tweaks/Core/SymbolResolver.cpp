#include "SymbolResolver.h"

namespace SSMT::Tweaks
{
    ResolvedSymbol ResolveSymbol(
        const PatternScanner &scanner,
        std::string_view pattern,
        ResolverKind kind,
        SymbolValidator validator)
    {
        ResolvedSymbol result{};
        result.kind = kind;
        const auto matches = scanner.FindAll(pattern);
        result.candidates = matches.size();

        std::size_t validated = 0;
        for (const auto match : matches)
        {
            if (!scanner.IsExecutableAddress(match) ||
                !scanner.IsReadableAddress(match) ||
                (validator && !validator(scanner, match))) continue;

            const auto target = kind == ResolverKind::RelativeBranch
                ? scanner.ResolveRelativeBranch(match) : match;
            if (!target || !scanner.IsExecutableAddress(target) ||
                !scanner.IsReadableAddress(target)) continue;

            ++validated;
            result.address = target;
        }

        if (validated != 1)
        {
            result.address = 0;
            result.validation = validated == 0 ? "no validated candidate" : "ambiguous validated candidates";
        }
        else
        {
            result.validation = "module, page protection and local layout verified";
        }
        return result;
    }
}

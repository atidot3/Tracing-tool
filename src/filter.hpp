#pragma once
#include <string>
#include <string_view>
#include <optional>
#include <regex>
#include <algorithm>

// ASCII-only lowercasing without locale.
inline char tolower_ascii(char c) {
    return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
}

inline bool contains_icase_ascii(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return true;
    if (needle.size() > haystack.size()) return false;
    // naive but branchless-enough search; avoids allocs
    const auto n0 = tolower_ascii(needle.front());
    for (size_t i = 0, N = haystack.size(), M = needle.size(); i + M <= N; ++i) {
        if (tolower_ascii(haystack[i]) != n0) continue;
        size_t j = 1;
        for (; j < M; ++j) {
            if (tolower_ascii(haystack[i + j]) != tolower_ascii(needle[j])) break;
        }
        if (j == M) return true;
    }
    return false;
}

// Compiled filter that prefers fast substring search; optionally switches to regex.
struct CompiledFilter {
    std::string pattern;
    bool case_sensitive = false;
    bool use_regex = false;

    // cache
    std::optional<std::regex> rx;
    std::string lowered;

    void compile(std::string p, bool cs, bool regex_mode) {
        pattern = std::move(p);
        case_sensitive = cs;
        use_regex = regex_mode;
        rx.reset();
        lowered.clear();
        if (use_regex && !pattern.empty()) {
            auto flags = std::regex::ECMAScript;
            if (!case_sensitive) flags = (std::regex::flag_type)(flags | std::regex::icase);
            rx.emplace(pattern, flags);
        } else if (!case_sensitive) {
            lowered.resize(pattern.size());
            std::transform(pattern.begin(), pattern.end(), lowered.begin(), tolower_ascii);
        }
    }

    bool match(std::string_view s) const {
        if (pattern.empty()) return true;
        if (use_regex) {
            if (!rx) return true;
            return std::regex_search(s.begin(), s.end(), *rx);
        }
        if (case_sensitive) {
            return s.find(pattern) != std::string_view::npos;
        }
        return contains_icase_ascii(s, lowered);
    }
};

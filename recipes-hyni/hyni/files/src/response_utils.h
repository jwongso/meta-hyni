#ifndef RESPONSE_UTILS_H
#define RESPONSE_UTILS_H

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <numeric>

namespace hyni {

/**
INFO:__main__:Transcription:  So can you give me a time when you have to handle a very difficult customer?
INFO:__main__:Transcription:  or let's see a very difficult boss
**/

class [[nodiscard]] response_utils final {
#if defined(__GNUC__) || defined(__clang__)
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#else
#define likely(x) (x)
#define unlikely(x) (x)
#endif

public:
    [[nodiscard]] static std::vector<std::string> split_and_normalize(const std::string& text) noexcept {
        std::vector<std::string> result;
        if (text.empty()) return result;

        const int space_count = std::count(text.begin(), text.end(), ' ');
        result.reserve(space_count + 1);

        std::string current;
        current.reserve(32);

        const char* data = text.data();
        const int length = text.length();

        for (int i = 0; i < length; ++i) {
            const char& ch = data[i];
            if (is_filtered_char(ch)) {
                if (!current.empty()) {
                    result.emplace_back(std::move(current));
                    current.clear();
                }
            } else {
                current += ch;
            }
        }

        if (!current.empty()) {
            result.emplace_back(std::move(current));
        }

        if (result.capacity() > result.size() * 2) {
            result.shrink_to_fit();
        }
        return result;
    }

/*
    [[nodiscard]] static std::string merge_strings(std::string_view a, std::string_view b, int& best_match_index) noexcept {
        if (a.empty()) {
            best_match_index = -1;
            return std::string(b);
        }
        if (b.empty()) {
            best_match_index = -1;
            return std::string(a);
        }

        auto split_and_normalize = [](std::string_view str) -> std::vector<std::string_view> {
            std::vector<std::string_view> words;
            size_t start = 0, end = 0;
            while ((end = str.find(' ', start)) != std::string_view::npos) {
                if (end > start) words.emplace_back(str.substr(start, end - start));
                start = end + 1;
            }
            if (start < str.size()) words.emplace_back(str.substr(start));
            return words;
        };

        const auto base = split_and_normalize(a);
        const auto tail = split_and_normalize(b);

        if (base.empty()) {
            best_match_index = -1;
            return std::string(b);
        }
        if (tail.empty()) {
            best_match_index = -1;
            return std::string(a);
        }

        auto compute_trigram_hash = [](std::string_view a, std::string_view b, std::string_view c) noexcept -> uint64_t {
            return std::hash<std::string_view>{}(a) ^ (std::hash<std::string_view>{}(b) << 1) ^ (std::hash<std::string_view>{}(c) << 2);
        };

        std::unordered_map<uint64_t, int> trigram_index;
        for (size_t i = 0; i + 2 < base.size(); ++i) {
            trigram_index[compute_trigram_hash(base[i], base[i + 1], base[i + 2])] = i;
        }

        best_match_index = -1;
        for (size_t i = 0; i + 2 < tail.size(); ++i) {
            uint64_t hash = compute_trigram_hash(tail[i], tail[i + 1], tail[i + 2]);
            if (trigram_index.count(hash)) {
                best_match_index = trigram_index[hash];
                break;
            }
        }

        std::string result;
        if (best_match_index > 0) {
            result.append(a.substr(0, base[best_match_index].data() - a.data()));
            // Only add space if the substring doesn't already end with one
            if (!result.empty() && result.back() != ' ') {
                result.push_back(' ');
            }
        } else {
            result.append(a);
            // Only add space if needed
            if (!result.empty() && result.back() != ' ') {
                result.push_back(' ');
            }
        }
        result.append(b);

        return result;
    }
*/
    [[nodiscard]] static std::string merge_strings_trigram(std::string_view a, std::string_view b, int& best_match_index) noexcept {
        if (a.empty()) {
            best_match_index = -1;
            return std::string(b);
        }
        if (b.empty()) {
            best_match_index = -1;
            return std::string(a);
        }

        auto split = [](std::string_view str) {
            std::vector<std::string_view> words;
            size_t start = 0;
            while (start < str.size()) {
                size_t end = str.find(' ', start);
                if (end == std::string_view::npos) end = str.size();
                if (end > start) words.push_back(str.substr(start, end - start));
                start = end + 1;
            }
            return words;
        };

        const auto base = split(a);
        const auto tail = split(b);

        // First check for complete prefix match (special case)
        if (base.size() >= 3 && tail.size() >= base.size()) {
            bool full_match = true;
            for (size_t i = 0; i < base.size(); ++i) {
                if (base[i] != tail[i]) {
                    full_match = false;
                    break;
                }
            }
            if (full_match) {
                best_match_index = 0;
                return std::string(b);
            }
        }

        // Proceed with trigram hash optimization for other cases
        best_match_index = -1;
        if (base.size() >= 3 && tail.size() >= 3) {
            auto compute_trigram_hash = [](std::string_view a, std::string_view b, std::string_view c) {
                return std::hash<std::string_view>{}(a) ^
                       (std::hash<std::string_view>{}(b) << 1) ^
                       (std::hash<std::string_view>{}(c) << 2);
            };

            // Build trigram index for base
            std::unordered_map<uint64_t, int> trigram_index;
            for (int i = 0; i <= static_cast<int>(base.size()) - 3; ++i) {
                trigram_index[compute_trigram_hash(base[i], base[i+1], base[i+2])] = i;
            }

            // Check for matching trigrams at start of tail
            uint64_t tail_hash = compute_trigram_hash(tail[0], tail[1], tail[2]);
            if (trigram_index.count(tail_hash)) {
                best_match_index = trigram_index[tail_hash];
            }
        }

        // Build the result string
        std::string result;
        if (best_match_index >= 0) {
            // Calculate position in original string
            size_t pos = 0;
            for (int i = 0; i < best_match_index; ++i) {
                pos += base[i].size() + 1; // +1 for space
            }
            result.append(a.substr(0, pos));

            if (!result.empty() && result.back() != ' ') {
                result.push_back(' ');
            }
        } else {
            result.append(a);
            if (!result.empty() && !b.empty() && result.back() != ' ') {
                result.push_back(' ');
            }
        }
        result.append(b);

        return result;
    }

    [[nodiscard]] static std::string merge_strings(
        std::string_view a,
        std::string_view b,
        int& best_match_index,
        int max_lookback_words = 8  // Only check last N words of 'a'
    ) noexcept {
        if (a.empty()) return std::string(b);
        if (b.empty()) return std::string(a);

        auto split = [](std::string_view str) {
            std::vector<std::string_view> words;
            size_t start = 0;
            while (start < str.size()) {
                size_t end = str.find(' ', start);
                if (end == std::string_view::npos) end = str.size();
                if (end > start) words.push_back(str.substr(start, end - start));
                start = end + 1;
            }
            return words;
        };

        const auto base = split(a);
        const auto tail = split(b);

        // Only check the RECENT part of 'a' (last max_lookback_words words)
        const int recent_start = std::max(0, static_cast<int>(base.size()) - max_lookback_words);
        best_match_index = -1;

        // Phase 1: Bigram matching in recent context
        if (tail.size() >= 2) {
            for (int i = static_cast<int>(base.size()) - 2; i >= recent_start; --i) {
                if (i + 1 >= static_cast<int>(base.size())) continue;
                if (base[i] == tail[0] && base[i+1] == tail[1]) {
                    best_match_index = i;
                    break;
                }
            }
        }

        // Phase 2: Unigram fallback in recent context
        if (best_match_index == -1 && !tail.empty()) {
            for (int i = static_cast<int>(base.size()) - 1; i >= recent_start; --i) {
                if (base[i] == tail[0]) {
                    best_match_index = i;
                    break;
                }
            }
        }

        // Merge logic (same as before)
        std::string result;
        if (best_match_index >= 0) {
            size_t pos = 0;
            for (int i = 0; i < best_match_index; ++i) {
                pos += base[i].size();
                if (i < best_match_index - 1) {
                    pos += 1;
                }
            }
            result = std::string(a.substr(0, pos));
            if (!b.empty()) {
                if (!result.empty() && result.back() != ' ') {
                    result += ' ';
                }
                result += b;
            }
        } else {
            result = std::string(a);
            if (!result.empty() && !b.empty()) {
                if (result.back() != ' ') result += ' ';
                result += b;
            }
        }

        return result;
    }

    [[nodiscard]] static std::string base64_encode(const unsigned char* data, size_t len) {
        static constexpr char encoding_table[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz"
            "0123456789+/";

        std::string ret;
        ret.reserve(4 * ((len + 2) / 3));

        for (size_t i = 0; i < len;) {
            size_t bytes_left = len - i;

            uint32_t octet_a = data[i++];
            uint32_t octet_b = (bytes_left > 1) ? data[i++] : 0;
            uint32_t octet_c = (bytes_left > 2) ? data[i++] : 0;

            uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

            ret += encoding_table[(triple >> 18) & 0x3F];
            ret += encoding_table[(triple >> 12) & 0x3F];
            ret += (bytes_left > 1) ? encoding_table[(triple >> 6) & 0x3F] : '=';
            ret += (bytes_left > 2) ? encoding_table[triple & 0x3F] : '=';
        }

        return ret;
    }

    [[nodiscard]] static std::string base64_encode(std::string_view input) {
        return base64_encode(reinterpret_cast<const unsigned char*>(input.data()), input.size());
    }

private:
    [[nodiscard]] static constexpr bool is_filtered_char(char ch) noexcept {
        const unsigned char uc = static_cast<unsigned char>(ch);
        return (uc == ' ') | (uc == ',') | (uc == '.') | (uc == ';') | (uc == '-');
    }

    [[nodiscard]] static int find_best_match(
        const std::vector<std::string>& tail,
        const std::vector<std::string>& base,
        const std::unordered_map<uint64_t, std::vector<int>>& trigram_index) noexcept {

        if (tail.size() < 3) return -1;

        for (size_t i = 0; i + 2 < tail.size(); ++i) {
            if (tail[i].empty() || tail[i + 1].empty() || tail[i + 2].empty()) {
                continue;
            }

            auto compute_trigram_hash = [](const std::string& a, const std::string& b, const std::string& c) noexcept {
                size_t h1 = std::hash<std::string>{}(a);
                size_t h2 = std::hash<std::string>{}(b);
                size_t h3 = std::hash<std::string>{}(c);
                return ((h1 * 0xFEA5B) ^ (h2 * 0x8DA6B) ^ (h3 * 0x7A97C)) * 0x9E3779B9;
            };

            const uint64_t tail_hash = compute_trigram_hash(tail[i], tail[i + 1], tail[i + 2]);

            if (const auto it = trigram_index.find(tail_hash); it != trigram_index.end()) {
                for (int base_index : it->second) {
                    if (static_cast<size_t>(base_index) + 2 < base.size() &&
                        tail[i] == base[base_index] &&
                        tail[i + 1] == base[base_index + 1] &&
                        tail[i + 2] == base[base_index + 2]) {
                        return base_index;
                    }
                }
            }
        }

        return -1;
    }

    [[nodiscard]] static int calculate_result_size(
        const std::string& a, const std::string& b,
        const std::vector<std::string>& base, const std::vector<std::string>& tail,
        int best_match_index) noexcept {

        if (best_match_index > 0) {
            int size = std::accumulate(base.begin(), base.begin() + best_match_index, 0,
                                       [](int sum, const std::string& s) {
                                           return sum + static_cast<int>(s.length());
                                       });

            return size + best_match_index - 1 + 1 + static_cast<int>(b.length());
        }

        return static_cast<int>(a.length()) + (a.empty() || a.back() == ' ' ? 0 : 1) + static_cast<int>(b.length());
    }
};

} // namespace hyni

#endif // RESPONSE_UTILS_H

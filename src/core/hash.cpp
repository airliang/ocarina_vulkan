//
// Created by Zero on 30/04/2022.
//

#include "hash.h"
#include "stl.h"
#include "fmt/bundled/core.h"

namespace ocarina {

string_view hash_to_string(uint64_t hash) noexcept {
    static thread_local ocarina::array<char, 17u> temp;
    fmt::format_to_n(temp.data(), 16u, "{:016X}", hash);
    temp[16] = '\0';
    return string_view{temp.data(), 16u};
}

}// namespace ocarina
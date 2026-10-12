#pragma once

#include "core/header.h"
#include <deque>
#include <list>
#include <map>
#include <queue>
#include <set>
#include <stack>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <EASTL/allocator.h>

// Requires ocarina::allocator from stl.h — include this file only at the end of stl.h.

namespace ocarina {

#if defined(OCARINA_ENABLE_TRACY) && OCARINA_ENABLE_TRACY

/// Routes heap blocks through EASTL/mimalloc and tags Tracy Memory pool (nullptr → Default).
/// Pool tagging is applied inside detail::allocator_allocate/deallocate — do not wrap
/// new/delete with TracyMemoryPoolScope at call sites.
template<typename T>
struct ocarina_pool_allocator {
    using value_type = T;

    const char* pool_name = nullptr;

    ocarina_pool_allocator() noexcept = default;
    explicit ocarina_pool_allocator(const char* name) noexcept : pool_name(name) {}

    template<typename U>
    ocarina_pool_allocator(const ocarina_pool_allocator<U>& other) noexcept : pool_name(other.pool_name) {}

    [[nodiscard]] T* allocate(std::size_t n) const noexcept {
        return static_cast<T*>(detail::allocator_allocate(sizeof(T) * n, alignof(T), pool_name));
    }

    void deallocate(T* p, std::size_t) const noexcept {
        detail::allocator_deallocate(p, alignof(T), pool_name);
    }

    template<typename U>
    [[nodiscard]] bool operator==(const ocarina_pool_allocator<U>& other) const noexcept {
        return pool_name == other.pool_name;
    }
};

#else

template<typename T>
struct ocarina_pool_allocator : std::allocator<T> {
    ocarina_pool_allocator() noexcept = default;
    explicit ocarina_pool_allocator(const char*) noexcept {}
    template<typename U>
    ocarina_pool_allocator(const ocarina_pool_allocator<U>&) noexcept : std::allocator<T>() {}
};

#endif

template<typename T, typename Alloc = ocarina_pool_allocator<T>>
using ocarina_vector = std::vector<T, Alloc>;

template<typename T, typename Alloc = ocarina_pool_allocator<T>>
using ocarina_list = std::list<T, Alloc>;

template<typename T, typename Alloc = ocarina_pool_allocator<T>>
using ocarina_deque = std::deque<T, Alloc>;

/// Like std::string, but heap goes through ocarina_pool_allocator → EASTL/mimalloc (+ Tracy).
using ocarina_string =
    std::basic_string<char, std::char_traits<char>, ocarina_pool_allocator<char>>;

using ocarina_wstring =
    std::basic_string<wchar_t, std::char_traits<wchar_t>, ocarina_pool_allocator<wchar_t>>;

template<typename Key, typename T, typename Compare = std::less<Key>,
         typename Alloc = ocarina_pool_allocator<std::pair<const Key, T>>>
using ocarina_map = std::map<Key, T, Compare, Alloc>;

template<typename Key, typename T, typename Compare = std::less<Key>,
         typename Alloc = ocarina_pool_allocator<std::pair<const Key, T>>>
using ocarina_multimap = std::multimap<Key, T, Compare, Alloc>;

template<typename Key, typename Compare = std::less<Key>,
         typename Alloc = ocarina_pool_allocator<Key>>
using ocarina_set = std::set<Key, Compare, Alloc>;

template<typename Key, typename T, typename Hash = std::hash<Key>,
         typename KeyEqual = std::equal_to<Key>,
         typename Alloc = ocarina_pool_allocator<std::pair<const Key, T>>>
using ocarina_unordered_map = std::unordered_map<Key, T, Hash, KeyEqual, Alloc>;

template<typename Key, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>,
         typename Alloc = ocarina_pool_allocator<Key>>
using ocarina_unordered_set = std::unordered_set<Key, Hash, KeyEqual, Alloc>;

template<typename T, typename Container = ocarina_deque<T>>
using ocarina_queue = std::queue<T, Container>;

template<typename T, typename Container = ocarina_deque<T>>
using ocarina_stack = std::stack<T, Container>;

// Short names inside namespace ocarina (same types as ocarina_*).
using string = ocarina_string;
using wstring = ocarina_wstring;

template<typename T, typename Alloc = ocarina_pool_allocator<T>>
using vector = ocarina_vector<T, Alloc>;

template<typename T, typename Alloc = ocarina_pool_allocator<T>>
using list = ocarina_list<T, Alloc>;

template<typename T, typename Alloc = ocarina_pool_allocator<T>>
using deque = ocarina_deque<T, Alloc>;

template<typename Key, typename T, typename Compare = std::less<Key>,
         typename Alloc = ocarina_pool_allocator<std::pair<const Key, T>>>
using map = ocarina_map<Key, T, Compare, Alloc>;

template<typename Key, typename T, typename Compare = std::less<Key>,
         typename Alloc = ocarina_pool_allocator<std::pair<const Key, T>>>
using multimap = ocarina_multimap<Key, T, Compare, Alloc>;

template<typename Key, typename Compare = std::less<Key>,
         typename Alloc = ocarina_pool_allocator<Key>>
using set = ocarina_set<Key, Compare, Alloc>;

template<typename Key, typename T, typename Hash = std::hash<Key>,
         typename KeyEqual = std::equal_to<Key>,
         typename Alloc = ocarina_pool_allocator<std::pair<const Key, T>>>
using unordered_map = ocarina_unordered_map<Key, T, Hash, KeyEqual, Alloc>;

template<typename Key, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>,
         typename Alloc = ocarina_pool_allocator<Key>>
using unordered_set = ocarina_unordered_set<Key, Hash, KeyEqual, Alloc>;

template<typename T, typename Container = ocarina_deque<T>>
using queue = ocarina_queue<T, Container>;

template<typename T, typename Container = ocarina_deque<T>>
using stack = ocarina_stack<T, Container>;

template<typename T>
[[nodiscard]] inline ocarina_vector<T> make_ocarina_vector(const char* pool_name) {
    return ocarina_vector<T>(ocarina_pool_allocator<T>(pool_name));
}

[[nodiscard]] inline ocarina_string make_ocarina_string(const char* pool_name) {
    return ocarina_string(ocarina_pool_allocator<char>(pool_name));
}

[[nodiscard]] inline ocarina_string to_ocarina_string(std::string_view sv) {
    return ocarina_string(sv.data(), sv.size());
}

}// namespace ocarina

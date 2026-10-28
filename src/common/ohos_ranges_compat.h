// SPDX-License-Identifier: GPL-2.0-or-later
// OHOS 的 libc++ 15 只有 <ranges> 的 views，没有 ranges 算法。
// 这里用经典 STL 算法补齐 Citron 用到的子集。
#pragma once

#include <algorithm>
#include <functional>
#include <iterator>
#include <ranges>
#include <utility>

#if __cplusplus >= 201703L

namespace std::ranges {

template <class R, class T>
constexpr auto find(R&& r, const T& value) {
    return std::find(std::begin(r), std::end(r), value);
}

template <class R, class Pred>
constexpr auto find_if(R&& r, Pred pred) {
    auto first = std::begin(r);
    auto last = std::end(r);
    for (; first != last; ++first) {
        if (std::invoke(pred, *first)) {
            break;
        }
    }
    return first;
}

template <class R, class Pred>
constexpr auto find_if_not(R&& r, Pred pred) {
    auto first = std::begin(r);
    auto last = std::end(r);
    for (; first != last; ++first) {
        if (!std::invoke(pred, *first)) {
            break;
        }
    }
    return first;
}

template <class R, class Pred>
constexpr auto any_of(R&& r, Pred pred) {
    for (auto&& e : r) {
        if (std::invoke(pred, e)) {
            return true;
        }
    }
    return false;
}

template <class R, class Pred>
constexpr auto all_of(R&& r, Pred pred) {
    for (auto&& e : r) {
        if (!std::invoke(pred, e)) {
            return false;
        }
    }
    return true;
}

template <class R, class Pred>
constexpr auto none_of(R&& r, Pred pred) {
    return !any_of(std::forward<R>(r), pred);
}

template <class R, class T>
constexpr auto count(R&& r, const T& value) {
    return std::count(std::begin(r), std::end(r), value);
}

template <class R, class Pred>
constexpr auto count_if(R&& r, Pred pred) {
    std::ptrdiff_t n = 0;
    for (auto&& e : r) {
        if (std::invoke(pred, e)) {
            ++n;
        }
    }
    return n;
}

template <class R, class T>
constexpr void fill(R&& r, const T& value) {
    std::fill(std::begin(r), std::end(r), value);
}

template <class R, class O>
constexpr auto copy(R&& r, O out) {
    return std::copy(std::begin(r), std::end(r), out);
}

template <class R, class O, class F>
constexpr auto transform(R&& r, O out, F f) {
    for (auto&& e : r) {
        *out++ = std::invoke(f, e);
    }
    return out;
}

template <class R1, class R2, class O, class F>
constexpr auto transform(R1&& r1, R2&& r2, O out, F f) {
    return std::transform(std::begin(r1), std::end(r1), std::begin(r2), out, std::move(f));
}

template <class R, class F>
constexpr F for_each(R&& r, F f) {
    for (auto&& e : r) {
        std::invoke(f, e);
    }
    return f;
}

template <class R, class G>
constexpr void generate(R&& r, G gen) {
    std::generate(std::begin(r), std::end(r), std::move(gen));
}

template <class R>
constexpr void sort(R&& r) {
    std::sort(std::begin(r), std::end(r));
}

template <class R, class Cmp>
constexpr void sort(R&& r, Cmp cmp) {
    std::sort(std::begin(r), std::end(r),
              [&cmp](auto& a, auto& b) { return std::invoke(cmp, a, b); });
}

template <class R>
constexpr void stable_sort(R&& r) {
    std::stable_sort(std::begin(r), std::end(r));
}

template <class R, class Cmp>
constexpr void stable_sort(R&& r, Cmp cmp) {
    std::stable_sort(std::begin(r), std::end(r),
                     [&cmp](auto& a, auto& b) { return std::invoke(cmp, a, b); });
}

template <class R>
constexpr void reverse(R&& r) {
    std::reverse(std::begin(r), std::end(r));
}

template <class R, class T>
constexpr void replace(R&& r, const T& old_value, const T& new_value) {
    std::replace(std::begin(r), std::end(r), old_value, new_value);
}

template <class R>
constexpr auto max_element(R&& r) {
    return std::max_element(std::begin(r), std::end(r));
}

template <class R, class Cmp>
constexpr auto max_element(R&& r, Cmp cmp) {
    return std::max_element(std::begin(r), std::end(r), std::move(cmp));
}

template <class R, class T>
constexpr auto lower_bound(R&& r, const T& value) {
    return std::lower_bound(std::begin(r), std::end(r), value);
}

template <class R, class T, class Cmp>
constexpr auto lower_bound(R&& r, const T& value, Cmp cmp) {
    return std::lower_bound(std::begin(r), std::end(r), value, std::move(cmp));
}

template <class R, class T>
constexpr auto upper_bound(R&& r, const T& value) {
    return std::upper_bound(std::begin(r), std::end(r), value);
}

template <class R, class T, class Cmp>
constexpr auto upper_bound(R&& r, const T& value, Cmp cmp) {
    return std::upper_bound(std::begin(r), std::end(r), value, std::move(cmp));
}

template <class R>
constexpr auto adjacent_find(R&& r) {
    return std::adjacent_find(std::begin(r), std::end(r));
}

template <class R, class Pred>
constexpr auto adjacent_find(R&& r, Pred pred) {
    return std::adjacent_find(std::begin(r), std::end(r), std::move(pred));
}


// ── 带 projection 的重载（Citron 里大量使用 ranges::find(..., &Member)） ──
template <class R, class T, class Proj>
constexpr auto find(R&& r, const T& value, Proj proj) {
    auto first = std::begin(r);
    auto last = std::end(r);
    for (; first != last; ++first) {
        if (std::invoke(proj, *first) == value) {
            break;
        }
    }
    return first;
}

template <class R, class Pred, class Proj>
constexpr auto find_if(R&& r, Pred pred, Proj proj) {
    auto first = std::begin(r);
    auto last = std::end(r);
    for (; first != last; ++first) {
        if (std::invoke(pred, std::invoke(proj, *first))) {
            break;
        }
    }
    return first;
}

template <class R, class Pred, class Proj>
constexpr auto any_of(R&& r, Pred pred, Proj proj) {
    for (auto&& e : r) {
        if (std::invoke(pred, std::invoke(proj, e))) {
            return true;
        }
    }
    return false;
}

template <class R, class Pred, class Proj>
constexpr auto all_of(R&& r, Pred pred, Proj proj) {
    for (auto&& e : r) {
        if (!std::invoke(pred, std::invoke(proj, e))) {
            return false;
        }
    }
    return true;
}

template <class R, class Pred, class Proj>
constexpr auto none_of(R&& r, Pred pred, Proj proj) {
    return !any_of(std::forward<R>(r), pred, proj);
}

template <class R, class Pred, class Proj>
constexpr auto count_if(R&& r, Pred pred, Proj proj) {
    std::ptrdiff_t n = 0;
    for (auto&& e : r) {
        if (std::invoke(pred, std::invoke(proj, e))) {
            ++n;
        }
    }
    return n;
}

template <class R, class Cmp, class Proj>
constexpr auto max_element(R&& r, Cmp cmp, Proj proj) {
    auto first = std::begin(r);
    auto last = std::end(r);
    if (first == last) {
        return first;
    }
    auto best = first;
    for (auto it = ++first; it != last; ++it) {
        if (std::invoke(cmp, std::invoke(proj, *best), std::invoke(proj, *it))) {
            best = it;
        }
    }
    return best;
}


// ── 带 compare + projection 的二分查找 ──
template <class R, class T, class Cmp, class Proj>
constexpr auto lower_bound_proj(R&& r, const T& value, Cmp cmp, Proj proj) {
    auto first = std::begin(r);
    auto last = std::end(r);
    auto n = last - first;
    while (n > 0) {
        const auto half = n / 2;
        auto mid = first + half;
        if (std::invoke(cmp, std::invoke(proj, *mid), value)) {
            first = mid + 1;
            n -= half + 1;
        } else {
            n = half;
        }
    }
    return first;
}

template <class R, class T, class Cmp, class Proj>
constexpr auto upper_bound_proj(R&& r, const T& value, Cmp cmp, Proj proj) {
    auto first = std::begin(r);
    auto last = std::end(r);
    auto n = last - first;
    while (n > 0) {
        const auto half = n / 2;
        auto mid = first + half;
        if (std::invoke(cmp, value, std::invoke(proj, *mid))) {
            n = half;
        } else {
            first = mid + 1;
            n -= half + 1;
        }
    }
    return first;
}

template <class R, class T, class Cmp, class Proj>
constexpr auto lower_bound(R&& r, const T& value, Cmp cmp, Proj proj) {
    return lower_bound_proj(std::forward<R>(r), value, cmp, proj);
}

template <class R, class T, class Cmp, class Proj>
constexpr auto upper_bound(R&& r, const T& value, Cmp cmp, Proj proj) {
    return upper_bound_proj(std::forward<R>(r), value, cmp, proj);
}

} // namespace std::ranges

// OHOS 的 libc++ 15 也缺 make_unique_for_overwrite
#include <cstddef>
#include <memory>
#include <type_traits>

namespace std {
template <class T>
    requires std::is_unbounded_array_v<T>
unique_ptr<T> make_unique_for_overwrite(std::size_t n) {
    return unique_ptr<T>(new std::remove_extent_t<T>[n]);
}

template <class T>
    requires(!std::is_array_v<T>)
unique_ptr<T> make_unique_for_overwrite() {
    return unique_ptr<T>(new T);
}
} // namespace std

// OHOS 的 musl 没有实现 pthread 取消相关接口，boost 等库会直接调用它们。
#ifdef __OHOS__
extern "C" inline int pthread_setcancelstate(int state, int* old_state) {
    (void)state;
    if (old_state) {
        *old_state = 0;
    }
    return 0;
}

extern "C" inline int pthread_setcanceltype(int type, int* old_type) {
    (void)type;
    if (old_type) {
        *old_type = 0;
    }
    return 0;
}

extern "C" inline int pthread_cancel(unsigned long) {
    return 0;
}
#endif

#endif // __cplusplus >= 201703L

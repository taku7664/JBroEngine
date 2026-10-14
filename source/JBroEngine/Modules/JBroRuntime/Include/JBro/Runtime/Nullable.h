#pragma once

#include <JBro/Types/Bool.h>

#include <cstddef>
#include <utility>

namespace JBro
{
    // Owned nullable value for JBroScript `ref T`. Both fields are reflected,
    // so scene save/undo can preserve absence as well as the stored value.
    template<typename T>
    struct Nullable
    {
        Bool present = false;
        T value{};

        Nullable() = default;
        Nullable(std::nullptr_t) noexcept {}
        Nullable(const T& initial) : present(true), value(initial) {}
        Nullable(T&& initial) : present(true), value(std::move(initial)) {}

        Bool HasValue() const noexcept { return present; }
        T& Value() noexcept { return value; }
        const T& Value() const noexcept { return value; }
        void Clear() noexcept { present = false; }
    };
}

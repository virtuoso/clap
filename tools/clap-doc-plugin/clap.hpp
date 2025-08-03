// SPDX-License-Identifier: Apache-2.0
#ifndef __CLAP_HPP__
#define __CLAP_HPP__

#include <string>
#include <type_traits>

namespace clap {
    static const std::string BASE_URL{"https://github.com/virtuoso/clap/blob/main"};
    static bool verbose;

    // Utility functions
    template<std::ranges::range R>
    requires std::same_as<std::ranges::range_value_t<R>, char>
    std::string to_string(R &&r) { return { r.begin(), r.end() }; }

    template<std::ranges::range R>
    requires std::same_as<std::ranges::range_value_t<R>, char>
    std::string_view to_sv(R &&r) { return { r.begin(), r.end() }; }

    // Make std::map<> methods compare strings with string views
    struct less_sv {
        using is_transparent = void;

        template<typename LHS, typename RHS>
        bool operator()(const LHS& lhs, const RHS& rhs) const
        {
            return std::string_view(lhs) < std::string_view(rhs);
        }
    };

    // Base class that disables copy and move semantics
    class no_copy_no_move {
    protected:
        no_copy_no_move() = default;
        ~no_copy_no_move() = default;

        // No move constructor or assignment
        no_copy_no_move(no_copy_no_move &&cmt) = delete;
        no_copy_no_move &operator=(no_copy_no_move &&cmt) = delete;

        // No copy constructor or assignment
        no_copy_no_move(const no_copy_no_move &cmt) = delete;
        no_copy_no_move &operator=(const no_copy_no_move &cmt) = delete;
    };

    // enum wrapper that allows it to function as both a enum and an
    // integral type without explicit type casts; IOW, what we need
    // from enums to actually function
    template<typename enum_t>
        requires std::is_enum_v<enum_t>                     // enum_t must be a enum
    struct enum_integral {
        using underlying = std::underlying_type_t<enum_t>;  // backing integral type

        enum_t v;   // the value

        constexpr enum_integral(enum_t v) : v(v) {}

        constexpr explicit
        operator underlying() const             { return static_cast<underlying>(v); }

        constexpr
        operator size_t() const                 { return static_cast<size_t>(v); }

        constexpr bool
        operator==(enum_integral other) const   { return v == other.v; }

        constexpr operator enum_t() const       { return v; }
    };

    // A set container for enums in C++20 where enums are no longer
    // integral types exactly.
    //
    // This implements a set backed by a bitmask: no red-black trees,
    // no hash buckets, everything (except the constructor) is O(1).
    template <typename enum_t>
        requires std::is_enum_v<enum_t>
    class enum_set {
        // underlying and state_mask have to be the same
        using underlying = std::underlying_type_t<enum_t>;
        using state_mask = underlying;
        state_mask mask = 0;

    public:
        constexpr enum_set(std::initializer_list<enum_t> states)
        {
            // TODO: Yes, an overflow is absolutely possible, and yes,
            // mask can be a vector if need be, we just don't need it
            // at the moment.
            for (enum_t s: states)
                mask |= 1u << static_cast<underlying>(s);
        }

        constexpr bool contains(enum_t s) const
        {
            return mask & (1u << static_cast<underlying>(s));
        }

        constexpr state_mask raw() const { return mask; }
    };
} // namespace clap

#endif // __CLAP_HPP__

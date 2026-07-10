#pragma once

#include "defines.hpp"

#include <bit>

struct Magic
{
    u64 mask;
    u64 magic;
    const u64* attacks;
    u32 shift;

    constexpr usize index(u64 occ) const noexcept { return static_cast<usize>(((occ & mask) * magic) >> shift); }
};

extern Magic ROOK_MAGICS[64];
extern Magic BISHOP_MAGICS[64];

void init_magic() noexcept;

inline u64 rook_attacks(Square sq, u64 occ) noexcept
{
    [[assume(sq < 64)]];
    const Magic& m = ROOK_MAGICS[sq];
    return m.attacks[m.index(occ)];
}

inline u64 bishop_attacks(Square sq, u64 occ) noexcept
{
    [[assume(sq < 64)]];
    const Magic& m = BISHOP_MAGICS[sq];
    return m.attacks[m.index(occ)];
}

inline u64 queen_attacks(Square sq, u64 occ) noexcept { return rook_attacks(sq, occ) | bishop_attacks(sq, occ); }
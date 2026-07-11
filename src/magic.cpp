#include "magic.hpp"

Magic ROOK_MAGICS[64] = {};
Magic BISHOP_MAGICS[64] = {};

namespace
{
constexpr usize ROOK_TABLE_SIZE = 0x19000;
constexpr usize BISHOP_TABLE_SIZE = 0x1480;

u64 ROOK_TABLE[ROOK_TABLE_SIZE];
u64 BISHOP_TABLE[BISHOP_TABLE_SIZE];

// clang-format off

constexpr u64 ROOK_MAGIC_NUMBERS[64] = {
    0x0080001020400080, 0x0040001000200040, 0x0080081000200080, 0x0080040800100080, 0x0080020400080080,
    0x0080010200040080, 0x0080008001000200, 0x0080002040800100, 0x0000800020400080, 0x0000400020005000,
    0x0000801000200080, 0x0000800800100080, 0x0000800400080080, 0x0000800200040080, 0x0000800100020080,
    0x0000800040800100, 0x0000208000400080, 0x0000404000201000, 0x0000808010002000, 0x0000808008001000,
    0x0000808004000800, 0x0000808002000400, 0x0000010100020004, 0x0000020000408104, 0x0000208080004000,
    0x0000200040005000, 0x0000100080200080, 0x0000080080100080, 0x0000040080080080, 0x0000020080040080,
    0x0000010080800200, 0x0000800080004100, 0x0000204000800080, 0x0000200040401000, 0x0000100080802000,
    0x0000080080801000, 0x0000040080800800, 0x0000020080800400, 0x0000020001010004, 0x0000800040800100,
    0x0000204000808000, 0x0000200040008080, 0x0000100020008080, 0x0000080010008080, 0x0000040008008080,
    0x0000020004008080, 0x0000010002008080, 0x0000004081020004, 0x0000204000800080, 0x0000200040008080,
    0x0000100020008080, 0x0000080010008080, 0x0000040008008080, 0x0000020004008080, 0x0000800100020080,
    0x0000800041000080, 0x00FFFCDDFCED714A, 0x007FFCDDFCED714A, 0x003FFFCDFFD88096, 0x0000040810002101,
    0x0001000204080011, 0x0001000204000801, 0x0001000082000401, 0x0001FFFAABFAD1A2};

constexpr u64 BISHOP_MAGIC_NUMBERS[64] = {
    0x0002020202020200, 0x0002020202020000, 0x0004010202000000, 0x0004040080000000, 0x0001104000000000,
    0x0000821040000000, 0x0000410410400000, 0x0000104104104000, 0x0000040404040400, 0x0000020202020200,
    0x0000040102020000, 0x0000040400800000, 0x0000011040000000, 0x0000008210400000, 0x0000004104104000,
    0x0000002082082000, 0x0004000808080800, 0x0002000404040400, 0x0001000202020200, 0x0000800802004000,
    0x0000800400A00000, 0x0000200100884000, 0x0000400082082000, 0x0000200041041000, 0x0002080010101000,
    0x0001040008080800, 0x0000208004010400, 0x0000404004010200, 0x0000840000802000, 0x0000404002011000,
    0x0000808001041000, 0x0000404000820800, 0x0001041000202000, 0x0000820800101000, 0x0000104400080800,
    0x0000020080080080, 0x0000404040040100, 0x0000808100020100, 0x0001010100020800, 0x0000808080010400,
    0x0000820820004000, 0x0000410410002000, 0x0000082088001000, 0x0000002011000800, 0x0000080100400400,
    0x0001010101000200, 0x0002020202000400, 0x0001010101000200, 0x0000410410400000, 0x0000208208200000,
    0x0000002084100000, 0x0000000020880000, 0x0000001002020000, 0x0000040408020000, 0x0004040404040000,
    0x0002020202020000, 0x0000104104104000, 0x0000002082082000, 0x0000000020841000, 0x0000000000208800,
    0x0000000010020200, 0x0000000404080200, 0x0000040404040400, 0x0002020202020200};

constexpr u32 ROOK_BITS[64] = {
    12, 11, 11, 11, 11, 11, 11, 12, 11, 10, 10, 10, 10, 10, 10, 11, 11, 10, 10, 10, 10, 10,
    10, 11, 11, 10, 10, 10, 10, 10, 10, 11, 11, 10, 10, 10, 10, 10, 10, 11, 11, 10, 10, 10,
    10, 10, 10, 11, 11, 10, 10, 10, 10, 10, 10, 11, 12, 11, 11, 11, 11, 11, 11, 12};

constexpr u32 BISHOP_BITS[64] = {
    6, 5, 5, 5, 5, 5, 5, 6, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 7, 7, 7, 7,
    5, 5, 5, 5, 7, 9, 9, 7, 5, 5, 5, 5, 7, 9, 9, 7, 5, 5, 5, 5, 7, 7,
    7, 7, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 5, 5, 5, 5, 5, 5, 6};

// clang-format on

u64 rook_mask(usize sq)
{
    u64 mask = 0;
    i32 r = static_cast<i32>(sq / 8);
    i32 f = static_cast<i32>(sq % 8);

    for (i32 i = r + 1; i < 7; i++) mask |= (1ULL << (i * 8 + f));
    for (i32 i = r - 1; i > 0; i--) mask |= (1ULL << (i * 8 + f));
    for (i32 i = f + 1; i < 7; i++) mask |= (1ULL << (r * 8 + i));
    for (i32 i = f - 1; i > 0; i--) mask |= (1ULL << (r * 8 + i));
    return mask;
}

constexpr u64 bishop_mask(usize sq)
{
    u64 mask = 0;
    i32 r = static_cast<i32>(sq / 8);
    i32 f = static_cast<i32>(sq % 8);

    for (i32 i = 1; r + i < 7 && f + i < 7; i++) mask |= (1ULL << ((r + i) * 8 + f + i));
    for (i32 i = 1; r + i < 7 && f - i > 0; i++) mask |= (1ULL << ((r + i) * 8 + f - i));
    for (i32 i = 1; r - i > 0 && f + i < 7; i++) mask |= (1ULL << ((r - i) * 8 + f + i));
    for (i32 i = 1; r - i > 0 && f - i > 0; i++) mask |= (1ULL << ((r - i) * 8 + f - i));
    return mask;
}

u64 rook_attacks_slow(usize sq, u64 occ)
{
    u64 attacks = 0;
    i32 r = static_cast<i32>(sq / 8);
    i32 f = static_cast<i32>(sq % 8);

    for (i32 i = r + 1; i < 8; i++)
    {
        attacks |= (1ULL << (i * 8 + f));
        if (occ & (1ULL << (i * 8 + f))) break;
    }
    for (i32 i = r - 1; i >= 0; i--)
    {
        attacks |= (1ULL << (i * 8 + f));
        if (occ & (1ULL << (i * 8 + f))) break;
    }
    for (i32 i = f + 1; i < 8; i++)
    {
        attacks |= (1ULL << (r * 8 + i));
        if (occ & (1ULL << (r * 8 + i))) break;
    }
    for (i32 i = f - 1; i >= 0; i--)
    {
        attacks |= (1ULL << (r * 8 + i));
        if (occ & (1ULL << (r * 8 + i))) break;
    }
    return attacks;
}

constexpr u64 bishop_attacks_slow(usize sq, u64 occ)
{
    u64 attacks = 0;
    i32 r = static_cast<i32>(sq / 8);
    i32 f = static_cast<i32>(sq % 8);

    for (i32 i = 1; r + i < 8 && f + i < 8; i++)
    {
        attacks |= (1ULL << ((r + i) * 8 + f + i));
        if (occ & (1ULL << ((r + i) * 8 + f + i))) break;
    }
    for (i32 i = 1; r + i < 8 && f - i >= 0; i++)
    {
        attacks |= (1ULL << ((r + i) * 8 + f - i));
        if (occ & (1ULL << ((r + i) * 8 + f - i))) break;
    }
    for (i32 i = 1; r - i >= 0 && f + i < 8; i++)
    {
        attacks |= (1ULL << ((r - i) * 8 + f + i));
        if (occ & (1ULL << ((r - i) * 8 + f + i))) break;
    }
    for (i32 i = 1; r - i >= 0 && f - i >= 0; i++)
    {
        attacks |= (1ULL << ((r - i) * 8 + f - i));
        if (occ & (1ULL << ((r - i) * 8 + f - i))) break;
    }
    return attacks;
}

u64 index_to_occupancy(usize index, u64 mask)
{
    u64 occ = 0;
    u32 j = 0;
    while (mask != 0)
    {
        u32 lsb = static_cast<u32>(std::countr_zero(mask));
        if (index & (1ULL << j)) occ |= (1ULL << lsb);
        mask &= mask - 1;
        j++;
    }
    return occ;
}
} // namespace

void init_magic()
{
    u64* rook_ptr = ROOK_TABLE;
    u64* bishop_ptr = BISHOP_TABLE;

    for (usize sq = 0; sq < 64; sq++)
    {
        Magic& rook_m = ROOK_MAGICS[sq];
        rook_m.mask = rook_mask(sq);
        rook_m.magic = ROOK_MAGIC_NUMBERS[sq];
        rook_m.shift = 64 - ROOK_BITS[sq];
        rook_m.attacks = rook_ptr;

        usize rook_size = 1ULL << ROOK_BITS[sq];
        for (usize i = 0; i < rook_size; i++)
        {
            u64 occ = index_to_occupancy(i, rook_m.mask);
            usize idx = rook_m.index(occ);
            rook_ptr[idx] = rook_attacks_slow(sq, occ);
        }
        rook_ptr += rook_size;

        Magic& bishop_m = BISHOP_MAGICS[sq];
        bishop_m.mask = bishop_mask(sq);
        bishop_m.magic = BISHOP_MAGIC_NUMBERS[sq];
        bishop_m.shift = 64 - BISHOP_BITS[sq];
        bishop_m.attacks = bishop_ptr;

        usize bishop_size = 1ULL << BISHOP_BITS[sq];
        for (usize i = 0; i < bishop_size; i++)
        {
            u64 occ = index_to_occupancy(i, bishop_m.mask);
            usize idx = bishop_m.index(occ);
            bishop_ptr[idx] = bishop_attacks_slow(sq, occ);
        }
        bishop_ptr += bishop_size;
    }
}
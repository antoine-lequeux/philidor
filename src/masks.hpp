#pragma once

#include "defines.hpp"

#include <algorithm>
#include <array>

namespace priv
{
constexpr std::array<Bitboard, 64> build_knight_attacks()
{
    std::array<Bitboard, 64> attacks {};
    constexpr i32 dr[] = {2, 2, -2, -2, 1, 1, -1, -1};
    constexpr i32 df[] = {1, -1, 1, -1, 2, -2, 2, -2};

    for (u32 sq = 0; sq < 64; ++sq)
    {
        i32 r = static_cast<i32>(sq / 8);
        i32 f = static_cast<i32>(sq % 8);
        for (u32 i = 0; i < 8; ++i)
        {
            i32 rr = r + dr[i];
            i32 ff = f + df[i];
            if (rr >= 0 && rr < 8 && ff >= 0 && ff < 8) attacks[sq] |= (1ULL << static_cast<u32>(rr * 8 + ff));
        }
    }
    return attacks;
}

constexpr std::array<Bitboard, 64> build_king_attacks()
{
    std::array<Bitboard, 64> attacks {};
    for (u32 sq = 0; sq < 64; ++sq)
    {
        i32 r = static_cast<i32>(sq / 8);
        i32 f = static_cast<i32>(sq % 8);
        for (i32 dr = -1; dr <= 1; ++dr)
        {
            for (i32 df = -1; df <= 1; ++df)
            {
                if (dr == 0 && df == 0) continue;

                i32 rr = r + dr;
                i32 ff = f + df;
                if (rr >= 0 && rr < 8 && ff >= 0 && ff < 8) attacks[sq] |= (1ULL << static_cast<u32>(rr * 8 + ff));
            }
        }
    }
    return attacks;
}

constexpr std::array<std::array<Bitboard, 64>, 2> build_pawn_attacks()
{
    std::array<std::array<Bitboard, 64>, 2> attacks {};
    for (u32 sq = 0; sq < 64; ++sq)
    {
        u32 r = sq / 8;
        u32 f = sq % 8;

        if (r < 7)
        {
            if (f > 0) attacks[0][sq] |= (1ULL << (sq + 7));
            if (f < 7) attacks[0][sq] |= (1ULL << (sq + 9));
        }
        if (r > 0)
        {
            if (f > 0) attacks[1][sq] |= (1ULL << (sq - 9));
            if (f < 7) attacks[1][sq] |= (1ULL << (sq - 7));
        }
    }
    return attacks;
}

constexpr std::array<std::array<Bitboard, 64>, 64> build_between()
{
    std::array<std::array<Bitboard, 64>, 64> table {};
    for (u32 from = 0; from < 64; ++from)
    {
        for (u32 to = 0; to < 64; ++to)
        {
            if (from == to) continue;

            i32 fr = static_cast<i32>(from / 8), ff = static_cast<i32>(from % 8);
            i32 tr = static_cast<i32>(to / 8), tf = static_cast<i32>(to % 8);

            i32 dr = (tr > fr) - (tr < fr);
            i32 df = (tf > ff) - (tf < ff);

            i32 abs_r = (tr >= fr) ? (tr - fr) : (fr - tr);
            i32 abs_f = (tf >= ff) ? (tf - ff) : (ff - tf);

            if (dr == 0 || df == 0 || abs_r == abs_f)
                for (i32 r = fr + dr, f = ff + df; r != tr || f != tf; r += dr, f += df)
                    table[from][to] |= (1ULL << static_cast<u32>(r * 8 + f));
        }
    }
    return table;
}

constexpr std::array<std::array<Bitboard, 64>, 64> build_line()
{
    std::array<std::array<Bitboard, 64>, 64> table {};
    for (u32 from = 0; from < 64; ++from)
    {
        for (u32 to = 0; to < 64; ++to)
        {
            if (from == to) continue;

            i32 fr = static_cast<i32>(from / 8), ff = static_cast<i32>(from % 8);
            i32 tr = static_cast<i32>(to / 8), tf = static_cast<i32>(to % 8);

            i32 dr = (tr > fr) - (tr < fr);
            i32 df = (tf > ff) - (tf < ff);

            i32 abs_r = (tr >= fr) ? (tr - fr) : (fr - tr);
            i32 abs_f = (tf >= ff) ? (tf - ff) : (ff - tf);

            if (dr == 0 || df == 0 || abs_r == abs_f)
            {
                for (i32 r = fr, f = ff; r >= 0 && r < 8 && f >= 0 && f < 8; r += dr, f += df)
                    table[from][to] |= (1ULL << static_cast<u32>(r * 8 + f));

                for (i32 r = fr - dr, f = ff - df; r >= 0 && r < 8 && f >= 0 && f < 8; r -= dr, f -= df)
                    table[from][to] |= (1ULL << static_cast<u32>(r * 8 + f));
            }
        }
    }
    return table;
}

constexpr auto KNIGHT_ATTACKS = build_knight_attacks();
constexpr auto KING_ATTACKS = build_king_attacks();
constexpr auto PAWN_ATTACKS = build_pawn_attacks();
constexpr auto BETWEEN = build_between();
constexpr auto LINE = build_line();

constexpr std::array<std::array<Bitboard, 64>, 2> build_passed_pawn_masks()
{
    std::array<std::array<Bitboard, 64>, 2> masks {};
    for (u32 sq = 0; sq < 64; ++sq)
    {
        i32 r = static_cast<i32>(sq / 8);
        i32 f = static_cast<i32>(sq % 8);

        for (i32 rr = r + 1; rr < 8; ++rr)
            for (i32 ff = std::max(0, f - 1); ff <= std::min(7, f + 1); ++ff)
                masks[0][sq] |= (1ULL << static_cast<u32>(rr * 8 + ff));

        for (i32 rr = 0; rr < r; ++rr)
            for (i32 ff = std::max(0, f - 1); ff <= std::min(7, f + 1); ++ff)
                masks[1][sq] |= (1ULL << static_cast<u32>(rr * 8 + ff));
    }
    return masks;
}

constexpr auto PASSED_PAWN_MASKS = build_passed_pawn_masks();
} // namespace priv

constexpr Bitboard knight_attacks(Square sq)
{
    [[assume(sq < 64)]];
    return priv::KNIGHT_ATTACKS[sq];
}

constexpr Bitboard king_attacks(Square sq)
{
    [[assume(sq < 64)]];
    return priv::KING_ATTACKS[sq];
}

// Squares attacked by a pawn of this color on 'sq'.
constexpr Bitboard pawn_attacks(Square sq, Color color)
{
    [[assume(sq < 64)]];
    return priv::PAWN_ATTACKS[color_index(color)][sq];
}

// Squares in front of a pawn on 'sq' (same and adjacent files) that must be free of enemy pawns.
constexpr Bitboard passed_pawn_mask(Square sq, Color color)
{
    [[assume(sq < 64)]];
    return priv::PASSED_PAWN_MASKS[color_index(color)][sq];
}

// Squares strictly between two aligned squares.
constexpr Bitboard squares_between(Square from, Square to)
{
    [[assume(from < 64 && to < 64)]];
    return priv::BETWEEN[from][to];
}

// All squares on the line through two aligned squares, including those squares.
constexpr Bitboard line_through(Square from, Square to)
{
    [[assume(from < 64 && to < 64)]];
    return priv::LINE[from][to];
}
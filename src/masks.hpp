#pragma once

#include "defines.hpp"

#include <array>

namespace priv
{
constexpr std::array<Bitboard, 64> build_knight_attacks() noexcept
{
    std::array<Bitboard, 64> attacks{};
    constexpr int dr[] = {2, 2, -2, -2, 1, 1, -1, -1};
    constexpr int df[] = {1, -1, 1, -1, 2, -2, 2, -2};

    for (int sq = 0; sq < 64; ++sq)
    {
        int r = sq / 8;
        int f = sq % 8;
        for (int i = 0; i < 8; ++i)
        {
            int rr = r + dr[i];
            int ff = f + df[i];
            if (rr >= 0 && rr < 8 && ff >= 0 && ff < 8)
                attacks[sq] |= (1ULL << (rr * 8 + ff));
        }
    }
    return attacks;
}

constexpr std::array<Bitboard, 64> build_king_attacks() noexcept
{
    std::array<Bitboard, 64> attacks{};
    for (int sq = 0; sq < 64; ++sq)
    {
        int r = sq / 8;
        int f = sq % 8;
        for (int dr = -1; dr <= 1; ++dr)
        {
            for (int df = -1; df <= 1; ++df)
            {
                if (dr == 0 && df == 0)
                    continue;

                int rr = r + dr;
                int ff = f + df;
                if (rr >= 0 && rr < 8 && ff >= 0 && ff < 8)
                    attacks[sq] |= (1ULL << (rr * 8 + ff));
            }
        }
    }
    return attacks;
}

constexpr std::array<std::array<Bitboard, 64>, 2> build_pawn_attacks() noexcept
{
    std::array<std::array<Bitboard, 64>, 2> attacks{};
    for (int sq = 0; sq < 64; ++sq)
    {
        int r = sq / 8;
        int f = sq % 8;

        if (r < 7)
        {
            if (f > 0)
                attacks[0][sq] |= (1ULL << (sq + 7));
            if (f < 7)
                attacks[0][sq] |= (1ULL << (sq + 9));
        }
        if (r > 0)
        {
            if (f > 0)
                attacks[1][sq] |= (1ULL << (sq - 9));
            if (f < 7)
                attacks[1][sq] |= (1ULL << (sq - 7));
        }
    }
    return attacks;
}

constexpr std::array<std::array<Bitboard, 64>, 64> build_between() noexcept
{
    std::array<std::array<Bitboard, 64>, 64> table{};
    for (int from = 0; from < 64; ++from)
    {
        for (int to = 0; to < 64; ++to)
        {
            if (from == to)
                continue;

            int fr = from / 8, ff = from % 8;
            int tr = to / 8, tf = to % 8;

            int dr = (tr > fr) - (tr < fr);
            int df = (tf > ff) - (tf < ff);

            int abs_r = (tr >= fr) ? (tr - fr) : (fr - tr);
            int abs_f = (tf >= ff) ? (tf - ff) : (ff - tf);

            if (dr == 0 || df == 0 || abs_r == abs_f)
            {
                for (int r = fr + dr, f = ff + df; r != tr || f != tf; r += dr, f += df)
                    table[from][to] |= (1ULL << (r * 8 + f));
            }
        }
    }
    return table;
}

constexpr std::array<std::array<Bitboard, 64>, 64> build_line() noexcept
{
    std::array<std::array<Bitboard, 64>, 64> table{};
    for (int from = 0; from < 64; ++from)
    {
        for (int to = 0; to < 64; ++to)
        {
            if (from == to)
                continue;

            int fr = from / 8, ff = from % 8;
            int tr = to / 8, tf = to % 8;

            int dr = (tr > fr) - (tr < fr);
            int df = (tf > ff) - (tf < ff);

            int abs_r = (tr >= fr) ? (tr - fr) : (fr - tr);
            int abs_f = (tf >= ff) ? (tf - ff) : (ff - tf);

            if (dr == 0 || df == 0 || abs_r == abs_f)
            {
                for (int r = fr, f = ff; r >= 0 && r < 8 && f >= 0 && f < 8; r += dr, f += df)
                    table[from][to] |= (1ULL << (r * 8 + f));

                for (int r = fr - dr, f = ff - df; r >= 0 && r < 8 && f >= 0 && f < 8; r -= dr, f -= df)
                    table[from][to] |= (1ULL << (r * 8 + f));
            }
        }
    }
    return table;
}

inline constexpr auto KNIGHT_ATTACKS = build_knight_attacks();
inline constexpr auto KING_ATTACKS = build_king_attacks();
inline constexpr auto PAWN_ATTACKS = build_pawn_attacks();
inline constexpr auto BETWEEN = build_between();
inline constexpr auto LINE = build_line();
} // namespace priv

constexpr Bitboard knight_attacks(Square sq) noexcept
{
    [[assume(sq < 64)]];
    return priv::KNIGHT_ATTACKS[sq];
}

constexpr Bitboard king_attacks(Square sq) noexcept
{
    [[assume(sq < 64)]];
    return priv::KING_ATTACKS[sq];
}

// Squares attacked by a pawn of this color on 'sq'.
constexpr Bitboard pawn_attacks(Square sq, Color color) noexcept
{
    [[assume(sq < 64)]];
    [[assume(static_cast<u8>(color) < 2)]];
    return priv::PAWN_ATTACKS[static_cast<size_t>(color)][sq];
}

// Squares strictly between two aligned squares.
constexpr Bitboard squares_between(Square from, Square to) noexcept
{
    [[assume(from < 64 && to < 64)]];
    return priv::BETWEEN[from][to];
}

// All squares on the line through two aligned squares, including those squares.
constexpr Bitboard line_through(Square from, Square to) noexcept
{
    [[assume(from < 64 && to < 64)]];
    return priv::LINE[from][to];
}
#pragma once

#include <cstdint>
#include <format>
#include <string>

using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using usize = size_t;

using f32 = float;

using Piece = u8;

inline constexpr Piece EMPTY = 0;

enum class Type : u8
{
    EMPTY,
    PAWN,
    KNIGHT,
    BISHOP,
    ROOK,
    QUEEN,
    KING
};

enum class Color : u8
{
    WHITE,
    BLACK
};

inline constexpr Color operator!(Color c) noexcept { return static_cast<Color>(static_cast<u8>(c) ^ 1); }

inline constexpr u8 PIECE_TYPE_MASK = 0b0111;
inline constexpr u8 PIECE_COLOR_MASK = 0b1000;

inline constexpr Piece make_piece(Type type, Color color) noexcept
{
    return static_cast<Piece>(static_cast<u8>(type) | (static_cast<u8>(color) << 3));
}

inline constexpr Type get_piece_type(Piece p) noexcept { return static_cast<Type>(p & PIECE_TYPE_MASK); }

inline constexpr Color get_piece_color(Piece p) noexcept { return static_cast<Color>((p & PIECE_COLOR_MASK) >> 3); }

inline constexpr char piece_to_char(Piece p) noexcept
{
    static constexpr char ASCII_PIECES[15] = {'.', 'P', 'N', 'B', 'R', 'Q', 'K', '.',
                                              '.', 'p', 'n', 'b', 'r', 'q', 'k'};
    return p < 15 ? ASCII_PIECES[p] : '.';
}

using Square = u16;
inline constexpr Square NO_SQUARE = 64;

struct Move
{
    u16 bits = 0;

    static constexpr u16 ENPASSANT_CAPTURE_FLAG = 1;
    static constexpr u16 CASTLE_FLAG = 2;
    static constexpr u16 PAWN_TWO_UP_FLAG = 3;
    static constexpr u16 PROMOTE_TO_QUEEN_FLAG = 4;
    static constexpr u16 PROMOTE_TO_KNIGHT_FLAG = 5;
    static constexpr u16 PROMOTE_TO_ROOK_FLAG = 6;
    static constexpr u16 PROMOTE_TO_BISHOP_FLAG = 7;

    static constexpr u16 START_SQUARE_MASK = 0b0000000000111111;
    static constexpr u16 TARGET_SQUARE_MASK = 0b0000111111000000;
    static constexpr u16 FROM_TO_MASK = 0b1111000000000000;

    constexpr Move() noexcept : bits(0) {}
    explicit constexpr Move(u16 b) noexcept : bits(b) {}

    static constexpr Move make(Square start, Square target) noexcept { return Move(start | (target << 6)); }

    static constexpr Move make_with_flag(Square start, Square target, u16 flag) noexcept
    {
        return Move(start | (target << 6) | (flag << 12));
    }

    constexpr u16 get_start_square() const noexcept { return bits & START_SQUARE_MASK; }

    constexpr u16 get_target_square() const noexcept { return (bits & TARGET_SQUARE_MASK) >> 6; }

    constexpr u16 get_flag() const noexcept { return bits >> 12; }

    constexpr bool is_promotion() const noexcept { return get_flag() >= PROMOTE_TO_QUEEN_FLAG; }

    constexpr Type get_promotion_type() const noexcept
    {
        switch (get_flag())
        {
            case PROMOTE_TO_QUEEN_FLAG:
                return Type::QUEEN;
            case PROMOTE_TO_KNIGHT_FLAG:
                return Type::KNIGHT;
            case PROMOTE_TO_ROOK_FLAG:
                return Type::ROOK;
            case PROMOTE_TO_BISHOP_FLAG:
                return Type::BISHOP;
            default:
                return Type::QUEEN;
        }
    }

    constexpr usize from_to_index() const noexcept { return static_cast<usize>(bits & FROM_TO_MASK); }

    constexpr void reset() noexcept { bits = 0; }

    constexpr bool is_some() const noexcept { return bits > 0; }
    constexpr bool is_null() const noexcept { return bits == 0; }

    constexpr bool operator==(const Move& other) const noexcept = default;

    std::string to_uci() const
    {
        auto idx_to_coord = [](Square idx) -> std::string
        {
            char file = 'a' + (idx % 8);
            char rank = '1' + (idx / 8);
            return {file, rank};
        };

        std::string uci = idx_to_coord(get_start_square()) + idx_to_coord(get_target_square());

        switch (get_promotion_type())
        {
            case Type::BISHOP:
                uci += 'b';
                break;
            case Type::ROOK:
                uci += 'r';
                break;
            case Type::KNIGHT:
                uci += 'n';
                break;
            case Type::QUEEN:
                uci += 'q';
                break;
            default:
                break;
        }
        return uci;
    }
};

enum class CastlingRights : u8
{
    NONE = 0,
    WK = 1 << 0,
    WQ = 1 << 1,
    BK = 1 << 2,
    BQ = 1 << 3,
    WHITE_ANY = WK | WQ,
    BLACK_ANY = BK | BQ,
    ALL = WK | WQ | BK | BQ
};

inline constexpr CastlingRights operator|(CastlingRights lhs, CastlingRights rhs) noexcept
{
    return static_cast<CastlingRights>(static_cast<u8>(lhs) | static_cast<u8>(rhs));
}

inline constexpr CastlingRights operator&(CastlingRights lhs, CastlingRights rhs) noexcept
{
    return static_cast<CastlingRights>(static_cast<u8>(lhs) & static_cast<u8>(rhs));
}

inline constexpr CastlingRights operator~(CastlingRights cr) noexcept
{
    return static_cast<CastlingRights>(~static_cast<u8>(cr));
}

inline constexpr CastlingRights& operator&=(CastlingRights& lhs, CastlingRights rhs) noexcept
{
    lhs = lhs & rhs;
    return lhs;
}

inline constexpr CastlingRights& operator|=(CastlingRights& lhs, CastlingRights rhs) noexcept
{
    lhs = lhs | rhs;
    return lhs;
}

using Bitboard = u64;
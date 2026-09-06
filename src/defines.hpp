#pragma once

#include <bit>
#include <cstdint>
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
using f64 = double;

using Score = i32;

constexpr Score INF = 32000;
constexpr Score MATE_VALUE = 31000;
constexpr Score MATE_THRESHOLD = MATE_VALUE - 1000;
constexpr usize MAX_PLY = 128;

using Piece = u32;

constexpr Piece EMPTY = 0;

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

constexpr Color operator!(Color c)
{
    return static_cast<Color>(static_cast<u8>(c) ^ 1);
}

constexpr usize color_index(Color c)
{
    return static_cast<usize>(c);
}
constexpr usize type_index(Type t)
{
    return static_cast<usize>(t);
}
constexpr usize piece_type_index(Type t)
{
    return static_cast<usize>(t) - 1;
}

constexpr u32 PIECE_TYPE_MASK = 0b0111;
constexpr u32 PIECE_COLOR_MASK = 0b1000;

constexpr Piece make_piece(Type type, Color color)
{
    return static_cast<u32>(type) | (static_cast<u32>(color) << 3);
}

constexpr Type get_piece_type(Piece p)
{
    return static_cast<Type>(p & PIECE_TYPE_MASK);
}

constexpr Color get_piece_color(Piece p)
{
    return static_cast<Color>((p & PIECE_COLOR_MASK) >> 3);
}

constexpr char piece_to_char(Piece p)
{
    static constexpr char ASCII_PIECES[15] = {'.', 'P', 'N', 'B', 'R', 'Q', 'K', '.',
                                              '.', 'p', 'n', 'b', 'r', 'q', 'k'};
    return p < 15 ? ASCII_PIECES[p] : '.';
}

constexpr usize bb_index(usize type_idx, usize color_idx)
{
    [[assume(type_idx >= 1 && type_idx <= 6)]];
    [[assume(color_idx < 2)]];
    return (type_idx - 1) + 6 * color_idx;
}

constexpr usize bb_index(Type type, Color color)
{
    return bb_index(type_index(type), color_index(color));
}

constexpr usize bb_index(Type type, usize color_idx)
{
    return bb_index(type_index(type), color_idx);
}

constexpr usize bb_index(usize type_idx, Color color)
{
    return bb_index(type_idx, color_index(color));
}

using Square = u32;
constexpr Square NO_SQUARE = 64;

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
    static constexpr u16 CAPTURE_FLAG = 8;

    static constexpr u16 START_SQUARE_MASK = 0b0000000000111111;
    static constexpr u16 TARGET_SQUARE_MASK = 0b0000111111000000;
    static constexpr u16 FROM_TO_MASK = 0b0000111111111111;

    constexpr Move() : bits(0) {}
    explicit constexpr Move(u16 b) : bits(b) {}

    static constexpr Move make(Square start, Square target) { return Move(static_cast<u16>(start | (target << 6))); }

    static constexpr Move make(Square start, Square target, u16 flag)
    {
        return Move(static_cast<u16>(start | (target << 6) | (static_cast<u32>(flag) << 12)));
    }

    constexpr Square get_start_square() const { return bits & START_SQUARE_MASK; }

    constexpr Square get_target_square() const { return (bits & TARGET_SQUARE_MASK) >> 6; }

    constexpr u16 get_flag() const { return bits >> 12; }

    constexpr bool is_promotion() const { return (get_flag() & ~CAPTURE_FLAG) >= PROMOTE_TO_QUEEN_FLAG; }

    constexpr Type get_promotion_type() const
    {
        switch (get_flag() & ~CAPTURE_FLAG)
        {
            case PROMOTE_TO_QUEEN_FLAG: return Type::QUEEN;
            case PROMOTE_TO_KNIGHT_FLAG: return Type::KNIGHT;
            case PROMOTE_TO_ROOK_FLAG: return Type::ROOK;
            case PROMOTE_TO_BISHOP_FLAG: return Type::BISHOP;
            default: return Type::EMPTY;
        }
    }

    constexpr usize from_to_index() const { return bits & FROM_TO_MASK; }

    constexpr void reset() { bits = 0; }

    constexpr bool is_some() const { return bits > 0; }
    constexpr bool is_null() const { return bits == 0; }

    constexpr bool operator==(const Move& other) const = default;

    std::string to_uci() const
    {
        auto idx_to_coord = [](Square idx) -> std::string {
            char file = static_cast<char>('a' + (idx % 8));
            char rank = static_cast<char>('1' + (idx / 8));
            return {file, rank};
        };

        std::string uci = idx_to_coord(get_start_square()) + idx_to_coord(get_target_square());

        switch (get_promotion_type())
        {
            case Type::BISHOP: uci += 'b'; break;
            case Type::ROOK: uci += 'r'; break;
            case Type::KNIGHT: uci += 'n'; break;
            case Type::QUEEN: uci += 'q'; break;
            default: break;
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

constexpr CastlingRights operator|(CastlingRights lhs, CastlingRights rhs)
{
    return static_cast<CastlingRights>(static_cast<u8>(lhs) | static_cast<u8>(rhs));
}

constexpr CastlingRights operator&(CastlingRights lhs, CastlingRights rhs)
{
    return static_cast<CastlingRights>(static_cast<u8>(lhs) & static_cast<u8>(rhs));
}

constexpr CastlingRights operator~(CastlingRights cr)
{
    return static_cast<CastlingRights>(~static_cast<u8>(cr));
}

constexpr CastlingRights& operator&=(CastlingRights& lhs, CastlingRights rhs)
{
    lhs = lhs & rhs;
    return lhs;
}

constexpr CastlingRights& operator|=(CastlingRights& lhs, CastlingRights rhs)
{
    lhs = lhs | rhs;
    return lhs;
}

using Bitboard = u64;

struct Bitloop
{
    Bitboard bb;
    constexpr explicit Bitloop(Bitboard b) : bb(b) {}

    struct Iterator
    {
        Bitboard bb;
        constexpr bool operator!=(const Iterator& other) const { return bb != other.bb; }
        constexpr Iterator& operator++()
        {
            bb &= bb - 1;
            return *this;
        }
        constexpr Square operator*() const { return static_cast<Square>(std::countr_zero(bb)); }
    };

    constexpr Iterator begin() const { return {bb}; }
    constexpr Iterator end() const { return {0}; }
};

constexpr Bitboard FILE_A = 0x0101010101010101ULL;
constexpr Bitboard FILE_H = 0x8080808080808080ULL;

constexpr Bitboard RANK_1 = 0x00000000000000FFULL;
constexpr Bitboard RANK_3 = 0x0000000000FF0000ULL;
constexpr Bitboard RANK_6 = 0x0000FF0000000000ULL;
constexpr Bitboard RANK_8 = 0xFF00000000000000ULL;

constexpr Bitboard WHITE_OO_BLOCKERS = 0x60ULL;  // f1, g1
constexpr Bitboard WHITE_OOO_BLOCKERS = 0x0EULL; // b1, c1, d1

constexpr Bitboard BLACK_OO_BLOCKERS = 0x6000000000000000ULL;  // f8, g8
constexpr Bitboard BLACK_OOO_BLOCKERS = 0x0E00000000000000ULL; // b8, c8, d8

enum class GenType
{
    CAPTURES,
    QUIETS,
    ALL
};
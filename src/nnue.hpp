#pragma once

#include "defines.hpp"

struct Board;

#define ALIGN alignas(64)

namespace nnue_constants
{
constexpr usize HIDDEN = 256;
constexpr i32 SCALE = 400;
constexpr i16 QA = 255;
constexpr i16 QB = 64;
} // namespace nnue_constants

#pragma pack(push, 1)
struct FileNetworkWeights
{
    ALIGN i16 feature_weights[768][nnue_constants::HIDDEN];
    ALIGN i16 feature_biases[nnue_constants::HIDDEN];
    ALIGN i16 l1_weights[nnue_constants::HIDDEN * 2][32];
    ALIGN i16 l1_biases[32];
    ALIGN i16 l2_weights[32][32];
    ALIGN i16 l2_biases[32];
    ALIGN i16 l3_weights[32][1];
    ALIGN i16 l3_biases[32];
};
struct RuntimeNetworkWeights
{
    ALIGN i16 feature_weights[768][nnue_constants::HIDDEN];
    ALIGN i16 feature_biases[nnue_constants::HIDDEN];
    ALIGN i16 l1_weights[32][nnue_constants::HIDDEN * 2];
    ALIGN i16 l1_biases[32];
    ALIGN i16 l2_weights[32][32];
    ALIGN i16 l2_biases[32];
    ALIGN i16 l3_weights[1][32];
    ALIGN i16 l3_biases[32];
};
#pragma pack(pop)

static_assert(sizeof(FileNetworkWeights) == 428800, "File layout size mismatch.");
static_assert(sizeof(RuntimeNetworkWeights) == 428800, "Runtime layout size mismatch.");

struct Accumulator
{
    ALIGN i16 white[nnue_constants::HIDDEN];
    ALIGN i16 black[nnue_constants::HIDDEN];

    void refresh();
};

namespace NNUE
{
void init();

void add_piece(Accumulator& acc, usize color, usize piece_type, Square sq);
void remove_piece(Accumulator& acc, usize color, usize piece_type, Square sq);
void move_piece(Accumulator& acc, usize color, usize piece_type, Square from_sq, Square to_sq);

void update_full(Accumulator& acc, const Board& board);

Score evaluate(const Accumulator& acc, Color side_to_move);
} // namespace NNUE
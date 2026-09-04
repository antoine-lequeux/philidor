#include "nnue.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>

#include <eve/eve.hpp>
#include <eve/module/core.hpp>

namespace
{
constexpr int KING_BUCKET_MULTIPLIER = 640;
constexpr int PIECE_INDEX_MULTIPLIER = 64;

alignas(32) constexpr unsigned char network_data[] = {
#embed "network.nnue"
};

const Network& network = *reinterpret_cast<const Network*>(network_data);

constexpr usize get_feature_index(usize color, usize piece_type, Square square, Square king_square)
{
    const usize king_bucket = nnue_constants::KING_BUCKETS[king_square];
    const usize index = (piece_type * 2) + (color);

    return (KING_BUCKET_MULTIPLIER * king_bucket) + (PIECE_INDEX_MULTIPLIER * index) + square;
}
} // namespace

NNUE::NNUE()
{
    std::memset(m_state->accumulator, 0, sizeof(m_state->accumulator));
}

NNUE::NNUE(const NNUE& other)
{
    std::memcpy(m_state->accumulator, other.m_state->accumulator, sizeof(m_state->accumulator));
}

NNUE& NNUE::operator=(const NNUE& other)
{
    if (this != &other)
    {
        if (!m_state) m_state = std::make_unique<NNUEState>();
        std::memcpy(m_state->accumulator, other.m_state->accumulator, sizeof(m_state->accumulator));
    }
    return *this;
}

Score NNUE::evaluate(usize color, u32 ply) const
{
    // Layer 1
    i16 output_layer1[NNUE_SIZE * 2];

    activate_relu(m_state->accumulator[ply][color], output_layer1, NNUE_SIZE);
    activate_relu(m_state->accumulator[ply][color ^ 1], output_layer1 + NNUE_SIZE, NNUE_SIZE);

    // Layers 2,3,4
    i16 o2[ARCH[L3][ROW]];
    i16 o3[ARCH[L4][ROW]];
    i32 o4[1];

    compute_layer<i16, true>(output_layer1, o2, network.b2, network.w2, ARCH[L2][ROW], ARCH[L2][COL]);
    compute_layer<i16, true>(o2, o3, network.b3, network.w3, ARCH[L3][ROW], ARCH[L3][COL]);
    compute_layer<i32, false>(o3, o4, network.b4, network.w4, ARCH[L4][ROW], ARCH[L4][COL]);

    Score eval = (o4[0] * 120) / nnue_constants::QUANT_FACTOR_B;
    return std::clamp(eval, -MATE_THRESHOLD + 1, MATE_THRESHOLD - 1);
}

void NNUE::inputs_full_update(u32 ply, const std::array<Piece, 64>& pieces, const std::array<Square, 2>& kings)
{
    i16* acc_w = m_state->accumulator[ply][0];
    i16* acc_b = m_state->accumulator[ply][1];

    for (usize i = 0; i < NNUE_SIZE; i++)
    {
        acc_w[i] = network.b1[i];
        acc_b[i] = network.b1[i];
    }

    Square king_square_w = kings[color_index(Color::WHITE)];
    Square king_square_b = kings[color_index(Color::BLACK)];

    for (Square square = 0; square < 64; ++square)
    {
        Piece p = pieces[square];
        if (p != EMPTY)
        {
            Type type = get_piece_type(p);
            Color color = get_piece_color(p);
            if (type != Type::KING)
                inputs_add_piece(color_index(color), piece_type_index(type), square, ply, king_square_w, king_square_b);
        }
    }
}

void NNUE::inputs_add_piece(
    usize color, usize piece_type, Square square, u32 ply, Square king_square_w, Square king_square_b
)
{
    king_square_b ^= nnue_constants::BLACK_PERSPECTIVE_XOR;

    const Square square_w = square;
    const Square square_b = square ^ nnue_constants::BLACK_PERSPECTIVE_XOR;

    const usize feature_w = get_feature_index(color, piece_type, square_w, king_square_w);
    const usize feature_b = get_feature_index(color ^ 1, piece_type, square_b, king_square_b);

    assert(feature_w <= NNUE_FEATURES);
    assert(feature_b <= NNUE_FEATURES);

    i16* acc_w = m_state->accumulator[ply][0];
    i16* acc_b = m_state->accumulator[ply][1];

    const i16* weights_w = &network.w1[NNUE_SIZE * feature_w];
    const i16* weights_b = &network.w1[NNUE_SIZE * feature_b];

    for (usize i = 0; i < NNUE_SIZE; i++)
    {
        acc_w[i] += weights_w[i];
        acc_b[i] += weights_b[i];
    }
}

void NNUE::inputs_remove_piece(
    usize color, usize piece_type, Square square, u32 ply, Square king_square_w, Square king_square_b
)
{
    king_square_b ^= nnue_constants::BLACK_PERSPECTIVE_XOR;

    const Square square_w = square;
    const Square square_b = square ^ nnue_constants::BLACK_PERSPECTIVE_XOR;

    const usize feature_w = get_feature_index(color, piece_type, square_w, king_square_w);
    const usize feature_b = get_feature_index(color ^ 1, piece_type, square_b, king_square_b);

    assert(feature_w <= NNUE_FEATURES);
    assert(feature_b <= NNUE_FEATURES);

    i16* acc_w = m_state->accumulator[ply][0];
    i16* acc_b = m_state->accumulator[ply][1];

    const i16* weights_w = &network.w1[NNUE_SIZE * feature_w];
    const i16* weights_b = &network.w1[NNUE_SIZE * feature_b];

    for (usize i = 0; i < NNUE_SIZE; i++)
    {
        acc_w[i] -= weights_w[i];
        acc_b[i] -= weights_b[i];
    }
}

void NNUE::inputs_move_piece(
    usize color, usize piece_type, Square from_sq, Square to_sq, u32 ply, Square king_square_w, Square king_square_b
)
{
    king_square_b ^= nnue_constants::BLACK_PERSPECTIVE_XOR;

    const Square from_sq_w = from_sq;
    const Square from_sq_b = from_sq ^ nnue_constants::BLACK_PERSPECTIVE_XOR;

    const Square to_sq_w = to_sq;
    const Square to_sq_b = to_sq ^ nnue_constants::BLACK_PERSPECTIVE_XOR;

    const usize feature_from_w = get_feature_index(color, piece_type, from_sq_w, king_square_w);
    const usize feature_from_b = get_feature_index(color ^ 1, piece_type, from_sq_b, king_square_b);

    const usize feature_to_w = get_feature_index(color, piece_type, to_sq_w, king_square_w);
    const usize feature_to_b = get_feature_index(color ^ 1, piece_type, to_sq_b, king_square_b);

    assert(feature_from_w <= NNUE_FEATURES);
    assert(feature_from_b <= NNUE_FEATURES);

    assert(feature_to_w <= NNUE_FEATURES);
    assert(feature_to_b <= NNUE_FEATURES);

    i16* acc_w = m_state->accumulator[ply][0];
    i16* acc_b = m_state->accumulator[ply][1];

    const i16* weights_from_w = &network.w1[NNUE_SIZE * feature_from_w];
    const i16* weights_from_b = &network.w1[NNUE_SIZE * feature_from_b];
    const i16* weights_to_w = &network.w1[NNUE_SIZE * feature_to_w];
    const i16* weights_to_b = &network.w1[NNUE_SIZE * feature_to_b];

    for (usize i = 0; i < NNUE_SIZE; i++)
    {
        acc_w[i] -= weights_from_w[i];
        acc_b[i] -= weights_from_b[i];

        acc_w[i] += weights_to_w[i];
        acc_b[i] += weights_to_b[i];
    }
}

void NNUE::copy_accumulator(u32 from_ply, u32 to_ply)
{
    std::memcpy(&m_state->accumulator[to_ply], m_state->accumulator[from_ply], sizeof(m_state->accumulator[0]));
}

void NNUE::activate_relu(const i16* input, i16* output, usize size) const
{
    using eve::max;
    using eve::min;
    using eve::wide;

    constexpr usize N = eve::expected_cardinal_v<i16>;

    usize i = 0;
    for (; i + N <= size; i += N)
    {
        wide<i16> val(input + i);
        val = max(val, i16 {0});
        val = min(val, i16 {255});
        eve::store(val, output + i);
    }
    for (; i < size; i++) output[i] = std::clamp<i16>(input[i], 0, 255);
}

template <typename T, bool with_relu>
void NNUE::compute_layer(
    const i16* input_layer, T* output_layer, const i32* biases, const i16* weights, usize dim_input, usize dim_output
) const
{
    using eve::wide;
    constexpr usize N = eve::expected_cardinal_v<i16>;

    for (usize o = 0; o < dim_output; o++)
    {
        i32 sum = biases[o];
        const usize offset = o * dim_input;

        eve::wide<i32, eve::fixed<N>> sum_vec(0);

        usize i = 0;
        for (; i + N <= dim_input; i += N)
        {
            eve::wide<i16, eve::fixed<N>> input_vec(input_layer + i);
            eve::wide<i16, eve::fixed<N>> weights_vec(weights + offset + i);

            sum_vec += eve::convert(input_vec, eve::as<i32>()) * eve::convert(weights_vec, eve::as<i32>());
        }

        sum += eve::reduce(sum_vec);

        for (; i < dim_input; i++) sum += input_layer[i] * weights[offset + i];

        if constexpr (with_relu)
        {
            sum /= nnue_constants::QUANT_FACTOR_W; // Revert scaling
            output_layer[o] = static_cast<T>(std::clamp<i32>(sum, 0, 255));
        }
        else
        {
            output_layer[o] = static_cast<T>(sum);
        }
    }
}

template void NNUE::compute_layer<i16, true>(const i16*, i16*, const i32*, const i16*, usize, usize) const;
template void NNUE::compute_layer<i32, false>(const i16*, i32*, const i32*, const i16*, usize, usize) const;
#pragma once

#include "nnue_arch.hpp"

#include "defines.hpp"
#include <array>
#include <memory>

struct NNUEState
{
    alignas(32) i16 accumulator[512][2][NNUE_SIZE]; // [PLY][COLOR][NNUE_SIZE]
};

class NNUE
{
public:

    NNUE();

    NNUE(const NNUE& other);
    NNUE& operator=(const NNUE& other);
    ~NNUE() = default;

    Score evaluate(usize color, u32 ply) const;

    void inputs_full_update(u32 ply, const std::array<Piece, 64>& pieces, const std::array<Square, 2>& kings);
    void
    inputs_add_piece(usize color, usize piece_type, Square square, u32 ply, Square king_square_w, Square king_square_b);
    void inputs_remove_piece(
        usize color, usize piece_type, Square square, u32 ply, Square king_square_w, Square king_square_b
    );
    void inputs_move_piece(
        usize color, usize piece_type, Square from_sq, Square to_sq, u32 ply, Square king_square_w, Square king_square_b
    );

    void copy_accumulator(u32 from_ply, u32 to_ply);

private:

    void activate_relu(const i16* input, i16* output, usize size) const;

    template <typename T, bool with_relu>
    void compute_layer(
        const i16* input_layer, T* output_layer, const i32* biases, const i16* weights, usize dim_input,
        usize dim_output
    ) const;

private:

    std::unique_ptr<NNUEState> m_state = std::make_unique<NNUEState>();
};
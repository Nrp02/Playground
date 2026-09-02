from __future__ import annotations

from .board import Board
from .pieces import (
    BISHOP,
    EMPTY,
    KING,
    KNIGHT,
    PAWN,
    QUEEN,
    ROOK,
    WHITE,
    file_of,
    piece_color,
    piece_type,
    rank_of,
)

PIECE_VALUES = {
    EMPTY: 0,
    PAWN: 100,
    KNIGHT: 320,
    BISHOP: 330,
    ROOK: 500,
    QUEEN: 900,
    KING: 20000,
}

_PAWN_TABLE = [
    0, 0, 0, 0, 0, 0, 0, 0,
    50, 50, 50, 50, 50, 50, 50, 50,
    10, 10, 20, 30, 30, 20, 10, 10,
    5, 5, 10, 25, 25, 10, 5, 5,
    0, 0, 0, 20, 20, 0, 0, 0,
    5, -5, -10, 0, 0, -10, -5, 5,
    5, 10, 10, -20, -20, 10, 10, 5,
    0, 0, 0, 0, 0, 0, 0, 0,
]

_KNIGHT_TABLE = [
    -50, -40, -30, -30, -30, -30, -40, -50,
    -40, -20, 0, 0, 0, 0, -20, -40,
    -30, 0, 10, 15, 15, 10, 0, -30,
    -30, 5, 15, 20, 20, 15, 5, -30,
    -30, 0, 15, 20, 20, 15, 0, -30,
    -30, 5, 10, 15, 15, 10, 5, -30,
    -40, -20, 0, 5, 5, 0, -20, -40,
    -50, -40, -30, -30, -30, -30, -40, -50,
]

_BISHOP_TABLE = [
    -20, -10, -10, -10, -10, -10, -10, -20,
    -10, 0, 0, 0, 0, 0, 0, -10,
    -10, 0, 5, 10, 10, 5, 0, -10,
    -10, 5, 5, 10, 10, 5, 5, -10,
    -10, 0, 10, 10, 10, 10, 0, -10,
    -10, 10, 10, 10, 10, 10, 10, -10,
    -10, 5, 0, 0, 0, 0, 5, -10,
    -20, -10, -10, -10, -10, -10, -10, -20,
]

_ROOK_TABLE = [
    0, 0, 0, 0, 0, 0, 0, 0,
    5, 10, 10, 10, 10, 10, 10, 5,
    -5, 0, 0, 0, 0, 0, 0, -5,
    -5, 0, 0, 0, 0, 0, 0, -5,
    -5, 0, 0, 0, 0, 0, 0, -5,
    -5, 0, 0, 0, 0, 0, 0, -5,
    -5, 0, 0, 0, 0, 0, 0, -5,
    0, 0, 0, 5, 5, 0, 0, 0,
]

_QUEEN_TABLE = [
    -20, -10, -10, -5, -5, -10, -10, -20,
    -10, 0, 0, 0, 0, 0, 0, -10,
    -10, 0, 5, 5, 5, 5, 0, -10,
    -5, 0, 5, 5, 5, 5, 0, -5,
    0, 0, 5, 5, 5, 5, 0, -5,
    -10, 5, 5, 5, 5, 5, 0, -10,
    -10, 0, 5, 0, 0, 0, 0, -10,
    -20, -10, -10, -5, -5, -10, -10, -20,
]

_KING_MIDGAME_TABLE = [
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -20, -30, -30, -40, -40, -30, -30, -20,
    -10, -20, -20, -20, -20, -20, -20, -10,
    20, 20, 0, 0, 0, 0, 20, 20,
    20, 30, 10, 0, 0, 10, 30, 20,
]

_KING_ENDGAME_TABLE = [
    -50, -40, -30, -20, -20, -30, -40, -50,
    -30, -20, -10, 0, 0, -10, -20, -30,
    -30, -10, 20, 30, 30, 20, -10, -30,
    -30, -10, 30, 40, 40, 30, -10, -30,
    -30, -10, 30, 40, 40, 30, -10, -30,
    -30, -10, 20, 30, 30, 20, -10, -30,
    -30, -30, 0, 0, 0, 0, -30, -30,
    -50, -30, -30, -30, -30, -30, -30, -50,
]

PIECE_SQUARE_TABLES = {
    PAWN: _PAWN_TABLE,
    KNIGHT: _KNIGHT_TABLE,
    BISHOP: _BISHOP_TABLE,
    ROOK: _ROOK_TABLE,
    QUEEN: _QUEEN_TABLE,
}

_PHASE_WEIGHTS = {KNIGHT: 1, BISHOP: 1, ROOK: 2, QUEEN: 4}
_MAX_PHASE = 24


def _table_index(sq: int, color: int) -> int:
    rank = rank_of(sq)
    if color == WHITE:
        rank = 7 - rank
    return rank * 8 + file_of(sq)


def game_phase(board: Board) -> int:
    phase = 0
    for sq in range(128):
        if sq & 0x88:
            continue
        piece = board.squares[sq]
        if piece == EMPTY:
            continue
        phase += _PHASE_WEIGHTS.get(piece_type(piece), 0)
    return min(phase, _MAX_PHASE)


def evaluate(board: Board) -> int:
    score = 0
    phase = game_phase(board)
    for sq in range(128):
        if sq & 0x88:
            continue
        piece = board.squares[sq]
        if piece == EMPTY:
            continue
        color = piece_color(piece)
        ptype = piece_type(piece)
        index = _table_index(sq, color)
        if ptype == KING:
            mid = _KING_MIDGAME_TABLE[index]
            end = _KING_ENDGAME_TABLE[index]
            value = (mid * phase + end * (_MAX_PHASE - phase)) // _MAX_PHASE
        else:
            value = PIECE_VALUES[ptype] + PIECE_SQUARE_TABLES[ptype][index]
        if color == WHITE:
            score += value
        else:
            score -= value
    return score if board.side == WHITE else -score


def material_balance(board: Board) -> int:
    score = 0
    for sq in range(128):
        if sq & 0x88:
            continue
        piece = board.squares[sq]
        if piece == EMPTY or piece_type(piece) == KING:
            continue
        value = PIECE_VALUES[piece_type(piece)]
        score += value if piece_color(piece) == WHITE else -value
    return score

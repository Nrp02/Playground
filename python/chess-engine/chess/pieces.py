from __future__ import annotations

from typing import NamedTuple

EMPTY = 0
PAWN = 1
KNIGHT = 2
BISHOP = 3
ROOK = 4
QUEEN = 5
KING = 6

WHITE = 0
BLACK = 1

COLOR_SHIFT = 3

PIECE_LETTERS = ".PNBRQK"

WHITE_KINGSIDE = 1
WHITE_QUEENSIDE = 2
BLACK_KINGSIDE = 4
BLACK_QUEENSIDE = 8

FLAG_CAPTURE = 1
FLAG_PROMOTION = 2
FLAG_EN_PASSANT = 4
FLAG_CASTLE = 8
FLAG_DOUBLE_PUSH = 16

FILE_CHARS = "abcdefgh"
RANK_CHARS = "12345678"


class Move(NamedTuple):
    frm: int
    to: int
    promo: int = 0
    flags: int = 0


class Undo(NamedTuple):
    move: Move
    captured: int
    captured_square: int
    castling: int
    ep_square: int
    halfmove: int
    key: int


def make_piece(color: int, ptype: int) -> int:
    return ptype | (color << COLOR_SHIFT)


def piece_type(piece: int) -> int:
    return piece & 7


def piece_color(piece: int) -> int:
    return piece >> COLOR_SHIFT


def piece_to_char(piece: int) -> str:
    letter = PIECE_LETTERS[piece_type(piece)]
    return letter if piece_color(piece) == WHITE else letter.lower()


def char_to_piece(ch: str) -> int:
    upper = ch.upper()
    if upper not in PIECE_LETTERS or upper == ".":
        raise ValueError("bad piece character: " + ch)
    ptype = PIECE_LETTERS.index(upper)
    color = WHITE if ch.isupper() else BLACK
    return make_piece(color, ptype)


def square(file: int, rank: int) -> int:
    return rank * 16 + file


def file_of(sq: int) -> int:
    return sq & 7


def rank_of(sq: int) -> int:
    return sq >> 4


def on_board(sq: int) -> bool:
    return not (sq & 0x88)


def square_name(sq: int) -> str:
    return FILE_CHARS[file_of(sq)] + RANK_CHARS[rank_of(sq)]


def parse_square(name: str) -> int:
    if len(name) != 2 or name[0] not in FILE_CHARS or name[1] not in RANK_CHARS:
        raise ValueError("bad square: " + name)
    return square(FILE_CHARS.index(name[0]), RANK_CHARS.index(name[1]))


A1 = square(0, 0)
B1 = square(1, 0)
C1 = square(2, 0)
D1 = square(3, 0)
E1 = square(4, 0)
F1 = square(5, 0)
G1 = square(6, 0)
H1 = square(7, 0)
A8 = square(0, 7)
B8 = square(1, 7)
C8 = square(2, 7)
D8 = square(3, 7)
E8 = square(4, 7)
F8 = square(5, 7)
G8 = square(6, 7)
H8 = square(7, 7)

KNIGHT_DELTAS = (33, 31, 18, 14, -33, -31, -18, -14)
BISHOP_DELTAS = (17, 15, -15, -17)
ROOK_DELTAS = (16, 1, -1, -16)
KING_DELTAS = BISHOP_DELTAS + ROOK_DELTAS

SLIDING_DELTAS = {
    BISHOP: BISHOP_DELTAS,
    ROOK: ROOK_DELTAS,
    QUEEN: KING_DELTAS,
}

PROMOTION_PIECES = (QUEEN, ROOK, BISHOP, KNIGHT)

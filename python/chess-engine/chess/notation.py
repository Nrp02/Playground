from __future__ import annotations

from .board import Board
from .pieces import (
    FILE_CHARS,
    FLAG_CAPTURE,
    FLAG_CASTLE,
    FLAG_PROMOTION,
    KING,
    Move,
    PAWN,
    PIECE_LETTERS,
    RANK_CHARS,
    file_of,
    piece_type,
    rank_of,
    square_name,
)


def move_to_uci(move: Move) -> str:
    text = square_name(move.frm) + square_name(move.to)
    if move.promo:
        text += PIECE_LETTERS[move.promo].lower()
    return text


def move_to_san(board: Board, move: Move) -> str:
    piece = board.squares[move.frm]
    ptype = piece_type(piece)
    is_capture = bool(move.flags & FLAG_CAPTURE)
    if move.flags & FLAG_CASTLE:
        text = "O-O" if file_of(move.to) == 6 else "O-O-O"
    elif ptype == PAWN:
        if is_capture:
            text = FILE_CHARS[file_of(move.frm)] + "x" + square_name(move.to)
        else:
            text = square_name(move.to)
        if move.flags & FLAG_PROMOTION:
            text += "=" + PIECE_LETTERS[move.promo]
    else:
        text = PIECE_LETTERS[ptype]
        if ptype != KING:
            rivals = [
                other
                for other in board.generate_legal_moves()
                if other.to == move.to
                and other.frm != move.frm
                and piece_type(board.squares[other.frm]) == ptype
            ]
            if rivals:
                if all(file_of(other.frm) != file_of(move.frm) for other in rivals):
                    text += FILE_CHARS[file_of(move.frm)]
                elif all(rank_of(other.frm) != rank_of(move.frm) for other in rivals):
                    text += RANK_CHARS[rank_of(move.frm)]
                else:
                    text += square_name(move.frm)
        if is_capture:
            text += "x"
        text += square_name(move.to)
    board.make_move(move)
    if board.in_check():
        text += "#" if not board.generate_legal_moves() else "+"
    board.unmake_move()
    return text


def san_line(board: Board, moves: list[Move]) -> str:
    parts: list[str] = []
    played = 0
    for move in moves:
        parts.append(move_to_san(board, move))
        board.make_move(move)
        played += 1
    for _ in range(played):
        board.unmake_move()
    return " ".join(parts)

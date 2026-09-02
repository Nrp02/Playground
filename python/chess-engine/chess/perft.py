from __future__ import annotations

from .board import Board
from .notation import move_to_uci


def perft(board: Board, depth: int) -> int:
    if depth <= 0:
        return 1
    moves = board.generate_legal_moves()
    if depth == 1:
        return len(moves)
    total = 0
    for move in moves:
        board.make_move(move)
        total += perft(board, depth - 1)
        board.unmake_move()
    return total


def perft_divide(board: Board, depth: int) -> list[tuple[str, int]]:
    results: list[tuple[str, int]] = []
    for move in board.generate_legal_moves():
        board.make_move(move)
        results.append((move_to_uci(move), perft(board, depth - 1)))
        board.unmake_move()
    results.sort()
    return results

from __future__ import annotations

import time
from typing import NamedTuple

from .board import Board
from .evaluate import PIECE_VALUES, evaluate
from .pieces import FLAG_CAPTURE, FLAG_EN_PASSANT, FLAG_PROMOTION, Move, PAWN, piece_type

MATE_VALUE = 30000
MATE_THRESHOLD = 29000
MAX_PLY = 64

EXACT = 0
LOWER_BOUND = 1
UPPER_BOUND = 2


class TTEntry(NamedTuple):
    key: int
    depth: int
    score: int
    node_type: int
    move: Move | None


class SearchResult(NamedTuple):
    move: Move | None
    score: int
    depth: int
    nodes: int
    quiescence_nodes: int
    pv: list[Move]
    elapsed: float
    tt_hits: int
    cutoffs: int


class Searcher:
    def __init__(
        self,
        use_transposition_table: bool = True,
        use_move_ordering: bool = True,
        use_quiescence: bool = True,
    ) -> None:
        self.use_transposition_table = use_transposition_table
        self.use_move_ordering = use_move_ordering
        self.use_quiescence = use_quiescence
        self.table: dict[int, TTEntry] = {}
        self.nodes = 0
        self.quiescence_nodes = 0
        self.tt_hits = 0
        self.cutoffs = 0
        self.killers: list[list[Move | None]] = [[None, None] for _ in range(MAX_PLY)]
        self.pv_length = [0] * MAX_PLY
        self.pv_table = [[None] * MAX_PLY for _ in range(MAX_PLY)]

    def reset(self) -> None:
        self.table.clear()
        self.nodes = 0
        self.quiescence_nodes = 0
        self.tt_hits = 0
        self.cutoffs = 0
        self.killers = [[None, None] for _ in range(MAX_PLY)]

    def search(self, board: Board, depth: int) -> SearchResult:
        self.nodes = 0
        self.quiescence_nodes = 0
        self.tt_hits = 0
        self.cutoffs = 0
        self.killers = [[None, None] for _ in range(MAX_PLY)]
        start = time.perf_counter()
        best_move: Move | None = None
        best_score = 0
        pv: list[Move] = []
        for current in range(1, depth + 1):
            score = self._negamax(board, current, -MATE_VALUE, MATE_VALUE, 0)
            pv = self._principal_variation(board, current)
            if pv:
                best_move = pv[0]
            best_score = score
            if abs(score) > MATE_THRESHOLD and MATE_VALUE - abs(score) <= current:
                break
        elapsed = time.perf_counter() - start
        return SearchResult(
            best_move,
            best_score,
            depth,
            self.nodes,
            self.quiescence_nodes,
            pv,
            elapsed,
            self.tt_hits,
            self.cutoffs,
        )

    def _principal_variation(self, board: Board, depth: int) -> list[Move]:
        line = [m for m in self.pv_table[0][: self.pv_length[0]] if m is not None]
        if not self.use_transposition_table:
            return line
        walked: list[Move] = []
        seen: set[int] = set()
        limit = max(depth, len(line)) + 8
        while len(walked) < limit:
            entry = self.table.get(board.key)
            if entry is None or entry.move is None or board.key in seen:
                break
            seen.add(board.key)
            legal = board.generate_legal_moves()
            if entry.move not in legal:
                break
            walked.append(entry.move)
            board.make_move(entry.move)
        for _ in walked:
            board.unmake_move()
        return walked if len(walked) >= len(line) else line

    def _store(self, key: int, depth: int, score: int, node_type: int, move: Move | None, ply: int) -> None:
        if not self.use_transposition_table:
            return
        stored = score
        if stored > MATE_THRESHOLD:
            stored += ply
        elif stored < -MATE_THRESHOLD:
            stored -= ply
        existing = self.table.get(key)
        if existing is None or existing.depth <= depth:
            self.table[key] = TTEntry(key, depth, stored, node_type, move)

    def _probe(self, key: int, ply: int) -> TTEntry | None:
        if not self.use_transposition_table:
            return None
        entry = self.table.get(key)
        if entry is None or entry.key != key:
            return None
        score = entry.score
        if score > MATE_THRESHOLD:
            score -= ply
        elif score < -MATE_THRESHOLD:
            score += ply
        return entry._replace(score=score)

    def _order(self, board: Board, moves: list[Move], tt_move: Move | None, ply: int) -> list[Move]:
        if not self.use_move_ordering:
            return moves
        killer_a, killer_b = self.killers[ply]
        squares = board.squares

        def score(move: Move) -> int:
            if tt_move is not None and move == tt_move:
                return 1_000_000
            if move.flags & FLAG_CAPTURE:
                if move.flags & FLAG_EN_PASSANT:
                    victim = PAWN
                else:
                    victim = piece_type(squares[move.to])
                attacker = piece_type(squares[move.frm])
                value = 100_000 + PIECE_VALUES[victim] * 10 - PIECE_VALUES[attacker]
                if move.flags & FLAG_PROMOTION:
                    value += PIECE_VALUES[move.promo]
                return value
            if move.flags & FLAG_PROMOTION:
                return 95_000 + PIECE_VALUES[move.promo]
            if killer_a is not None and move == killer_a:
                return 90_000
            if killer_b is not None and move == killer_b:
                return 89_000
            return 0

        return sorted(moves, key=score, reverse=True)

    def _negamax(self, board: Board, depth: int, alpha: int, beta: int, ply: int) -> int:
        self.nodes += 1
        self.pv_length[ply] = ply
        original_alpha = alpha
        key = board.key
        tt_move: Move | None = None
        entry = self._probe(key, ply)
        if entry is not None:
            tt_move = entry.move
            if ply > 0 and entry.depth >= depth:
                self.tt_hits += 1
                if entry.node_type == EXACT:
                    return entry.score
                if entry.node_type == LOWER_BOUND and entry.score > alpha:
                    alpha = entry.score
                elif entry.node_type == UPPER_BOUND and entry.score < beta:
                    beta = entry.score
                if alpha >= beta:
                    self.cutoffs += 1
                    return entry.score

        if depth <= 0:
            if self.use_quiescence:
                return self._quiescence(board, alpha, beta, ply)
            return evaluate(board)

        moves = board.generate_legal_moves()
        if not moves:
            if board.in_check():
                return -MATE_VALUE + ply
            return 0

        moves = self._order(board, moves, tt_move, ply)
        best_score = -MATE_VALUE - 1
        best_move: Move | None = None
        for move in moves:
            board.make_move(move)
            score = -self._negamax(board, depth - 1, -beta, -alpha, ply + 1)
            board.unmake_move()
            if score > best_score:
                best_score = score
                best_move = move
                if ply + 1 < MAX_PLY:
                    self.pv_table[ply][ply] = move
                    for index in range(ply + 1, self.pv_length[ply + 1]):
                        self.pv_table[ply][index] = self.pv_table[ply + 1][index]
                    self.pv_length[ply] = self.pv_length[ply + 1]
            if best_score > alpha:
                alpha = best_score
            if alpha >= beta:
                self.cutoffs += 1
                if not (move.flags & FLAG_CAPTURE) and ply < MAX_PLY:
                    killers = self.killers[ply]
                    if killers[0] != move:
                        killers[1] = killers[0]
                        killers[0] = move
                break

        if best_score <= original_alpha:
            node_type = UPPER_BOUND
        elif best_score >= beta:
            node_type = LOWER_BOUND
        else:
            node_type = EXACT
        self._store(key, depth, best_score, node_type, best_move, ply)
        return best_score

    def _quiescence(self, board: Board, alpha: int, beta: int, ply: int) -> int:
        self.quiescence_nodes += 1
        if ply >= MAX_PLY - 1:
            return evaluate(board)
        in_check = board.in_check()
        if in_check:
            moves = board.generate_legal_moves()
            if not moves:
                return -MATE_VALUE + ply
        else:
            stand_pat = evaluate(board)
            if stand_pat >= beta:
                return beta
            if stand_pat > alpha:
                alpha = stand_pat
            moves = board.generate_legal_moves(captures_only=True)
            moves = [m for m in moves if m.flags & (FLAG_CAPTURE | FLAG_PROMOTION)]
            if not moves:
                return alpha
        moves = self._order(board, moves, None, min(ply, MAX_PLY - 1))
        for move in moves:
            board.make_move(move)
            score = -self._quiescence(board, -beta, -alpha, ply + 1)
            board.unmake_move()
            if score >= beta:
                return beta
            if score > alpha:
                alpha = score
        return alpha


def find_best_move(board: Board, depth: int) -> SearchResult:
    return Searcher().search(board, depth)


def score_text(score: int) -> str:
    if score > MATE_THRESHOLD:
        return f"mate in {(MATE_VALUE - score + 1) // 2}"
    if score < -MATE_THRESHOLD:
        return f"mated in {(MATE_VALUE + score + 1) // 2}"
    return f"{score / 100:+.2f}"

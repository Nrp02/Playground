from __future__ import annotations

from .pieces import (
    A1,
    A8,
    B1,
    B8,
    BISHOP,
    BISHOP_DELTAS,
    BLACK,
    BLACK_KINGSIDE,
    BLACK_QUEENSIDE,
    C1,
    C8,
    D1,
    D8,
    E1,
    E8,
    EMPTY,
    F1,
    F8,
    FLAG_CAPTURE,
    FLAG_CASTLE,
    FLAG_DOUBLE_PUSH,
    FLAG_EN_PASSANT,
    FLAG_PROMOTION,
    G1,
    G8,
    H1,
    H8,
    KING,
    KING_DELTAS,
    KNIGHT,
    KNIGHT_DELTAS,
    Move,
    PAWN,
    PROMOTION_PIECES,
    QUEEN,
    ROOK,
    ROOK_DELTAS,
    Undo,
    WHITE,
    WHITE_KINGSIDE,
    WHITE_QUEENSIDE,
    char_to_piece,
    file_of,
    make_piece,
    parse_square,
    piece_color,
    piece_to_char,
    piece_type,
    rank_of,
    square,
    square_name,
)
from .zobrist import CASTLING_KEYS, EP_FILE_KEYS, PIECE_KEYS, SIDE_KEY

START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

_CASTLE_MASK = [15] * 128
_CASTLE_MASK[A1] = 15 & ~WHITE_QUEENSIDE
_CASTLE_MASK[H1] = 15 & ~WHITE_KINGSIDE
_CASTLE_MASK[E1] = 15 & ~(WHITE_KINGSIDE | WHITE_QUEENSIDE)
_CASTLE_MASK[A8] = 15 & ~BLACK_QUEENSIDE
_CASTLE_MASK[H8] = 15 & ~BLACK_KINGSIDE
_CASTLE_MASK[E8] = 15 & ~(BLACK_KINGSIDE | BLACK_QUEENSIDE)

_CASTLE_ROOK = {
    G1: (H1, F1),
    C1: (A1, D1),
    G8: (H8, F8),
    C8: (A8, D8),
}

_PAWN_ATTACK_ORIGINS = {
    WHITE: (-17, -15),
    BLACK: (17, 15),
}


class Board:
    def __init__(self, fen: str = START_FEN) -> None:
        self.squares: list[int] = [EMPTY] * 128
        self.side: int = WHITE
        self.castling: int = 0
        self.ep_square: int = -1
        self.halfmove: int = 0
        self.fullmove: int = 1
        self.king_square: list[int] = [-1, -1]
        self.key: int = 0
        self.history: list[Undo] = []
        self.set_fen(fen)

    def set_fen(self, fen: str) -> None:
        parts = fen.split()
        if len(parts) < 4:
            raise ValueError("bad FEN: " + fen)
        placement, side, castling, ep = parts[0], parts[1], parts[2], parts[3]
        halfmove = int(parts[4]) if len(parts) > 4 else 0
        fullmove = int(parts[5]) if len(parts) > 5 else 1

        self.squares = [EMPTY] * 128
        self.king_square = [-1, -1]
        rank = 7
        file = 0
        for ch in placement:
            if ch == "/":
                rank -= 1
                file = 0
            elif ch.isdigit():
                file += int(ch)
            else:
                piece = char_to_piece(ch)
                sq = square(file, rank)
                self.squares[sq] = piece
                if piece_type(piece) == KING:
                    self.king_square[piece_color(piece)] = sq
                file += 1

        self.side = WHITE if side == "w" else BLACK
        self.castling = 0
        if "K" in castling:
            self.castling |= WHITE_KINGSIDE
        if "Q" in castling:
            self.castling |= WHITE_QUEENSIDE
        if "k" in castling:
            self.castling |= BLACK_KINGSIDE
        if "q" in castling:
            self.castling |= BLACK_QUEENSIDE
        self.ep_square = -1 if ep == "-" else parse_square(ep)
        self.halfmove = halfmove
        self.fullmove = fullmove
        self.history = []
        self.key = self.compute_key()

    def fen(self) -> str:
        rows = []
        for rank in range(7, -1, -1):
            row = ""
            gap = 0
            for file in range(8):
                piece = self.squares[square(file, rank)]
                if piece == EMPTY:
                    gap += 1
                else:
                    if gap:
                        row += str(gap)
                        gap = 0
                    row += piece_to_char(piece)
            if gap:
                row += str(gap)
            rows.append(row)
        placement = "/".join(rows)
        side = "w" if self.side == WHITE else "b"
        rights = ""
        if self.castling & WHITE_KINGSIDE:
            rights += "K"
        if self.castling & WHITE_QUEENSIDE:
            rights += "Q"
        if self.castling & BLACK_KINGSIDE:
            rights += "k"
        if self.castling & BLACK_QUEENSIDE:
            rights += "q"
        if not rights:
            rights = "-"
        ep = "-" if self.ep_square < 0 else square_name(self.ep_square)
        return f"{placement} {side} {rights} {ep} {self.halfmove} {self.fullmove}"

    def compute_key(self) -> int:
        key = 0
        for sq in range(128):
            if sq & 0x88:
                continue
            piece = self.squares[sq]
            if piece != EMPTY:
                key ^= PIECE_KEYS[piece][sq]
        if self.side == BLACK:
            key ^= SIDE_KEY
        key ^= CASTLING_KEYS[self.castling]
        if self.ep_square >= 0:
            key ^= EP_FILE_KEYS[file_of(self.ep_square)]
        return key

    def clone(self) -> "Board":
        return Board(self.fen())

    def __str__(self) -> str:
        lines = []
        for rank in range(7, -1, -1):
            row = [str(rank + 1), " "]
            for file in range(8):
                piece = self.squares[square(file, rank)]
                row.append("." if piece == EMPTY else piece_to_char(piece))
                row.append(" ")
            lines.append("".join(row).rstrip())
        lines.append("  a b c d e f g h")
        turn = "white" if self.side == WHITE else "black"
        lines.append("  " + turn + " to move")
        return "\n".join(lines)

    def is_attacked(self, sq: int, by_color: int) -> bool:
        squares = self.squares
        for delta in _PAWN_ATTACK_ORIGINS[by_color]:
            origin = sq + delta
            if not (origin & 0x88):
                piece = squares[origin]
                if piece and piece_color(piece) == by_color and piece_type(piece) == PAWN:
                    return True
        for delta in KNIGHT_DELTAS:
            origin = sq + delta
            if not (origin & 0x88):
                piece = squares[origin]
                if piece and piece_color(piece) == by_color and piece_type(piece) == KNIGHT:
                    return True
        for delta in KING_DELTAS:
            origin = sq + delta
            if not (origin & 0x88):
                piece = squares[origin]
                if piece and piece_color(piece) == by_color and piece_type(piece) == KING:
                    return True
        for delta in BISHOP_DELTAS:
            origin = sq + delta
            while not (origin & 0x88):
                piece = squares[origin]
                if piece:
                    if piece_color(piece) == by_color and piece_type(piece) in (BISHOP, QUEEN):
                        return True
                    break
                origin += delta
        for delta in ROOK_DELTAS:
            origin = sq + delta
            while not (origin & 0x88):
                piece = squares[origin]
                if piece:
                    if piece_color(piece) == by_color and piece_type(piece) in (ROOK, QUEEN):
                        return True
                    break
                origin += delta
        return False

    def in_check(self, color: int | None = None) -> bool:
        if color is None:
            color = self.side
        return self.is_attacked(self.king_square[color], color ^ 1)

    def generate_pseudo_legal(self, captures_only: bool = False) -> list[Move]:
        moves: list[Move] = []
        squares = self.squares
        color = self.side
        enemy = color ^ 1
        forward = 16 if color == WHITE else -16
        start_rank = 1 if color == WHITE else 6
        last_rank = 7 if color == WHITE else 0
        for sq in range(128):
            if sq & 0x88:
                continue
            piece = squares[sq]
            if piece == EMPTY or piece_color(piece) != color:
                continue
            ptype = piece_type(piece)
            if ptype == PAWN:
                one = sq + forward
                if not captures_only and not (one & 0x88) and squares[one] == EMPTY:
                    if rank_of(one) == last_rank:
                        for promo in PROMOTION_PIECES:
                            moves.append(Move(sq, one, promo, FLAG_PROMOTION))
                    else:
                        moves.append(Move(sq, one))
                        two = one + forward
                        if rank_of(sq) == start_rank and squares[two] == EMPTY:
                            moves.append(Move(sq, two, 0, FLAG_DOUBLE_PUSH))
                for side_delta in (-1, 1):
                    target = sq + forward + side_delta
                    if target & 0x88:
                        continue
                    victim = squares[target]
                    if victim != EMPTY and piece_color(victim) == enemy:
                        if rank_of(target) == last_rank:
                            for promo in PROMOTION_PIECES:
                                moves.append(
                                    Move(sq, target, promo, FLAG_PROMOTION | FLAG_CAPTURE)
                                )
                        else:
                            moves.append(Move(sq, target, 0, FLAG_CAPTURE))
                    elif victim == EMPTY and target == self.ep_square:
                        moves.append(
                            Move(sq, target, 0, FLAG_CAPTURE | FLAG_EN_PASSANT)
                        )
            elif ptype in (KNIGHT, KING):
                deltas = KNIGHT_DELTAS if ptype == KNIGHT else KING_DELTAS
                for delta in deltas:
                    target = sq + delta
                    if target & 0x88:
                        continue
                    victim = squares[target]
                    if victim == EMPTY:
                        if not captures_only:
                            moves.append(Move(sq, target))
                    elif piece_color(victim) == enemy:
                        moves.append(Move(sq, target, 0, FLAG_CAPTURE))
            else:
                if ptype == BISHOP:
                    deltas = BISHOP_DELTAS
                elif ptype == ROOK:
                    deltas = ROOK_DELTAS
                else:
                    deltas = KING_DELTAS
                for delta in deltas:
                    target = sq + delta
                    while not (target & 0x88):
                        victim = squares[target]
                        if victim == EMPTY:
                            if not captures_only:
                                moves.append(Move(sq, target))
                        else:
                            if piece_color(victim) == enemy:
                                moves.append(Move(sq, target, 0, FLAG_CAPTURE))
                            break
                        target += delta
        if not captures_only:
            self._add_castles(moves)
        return moves

    def _add_castles(self, moves: list[Move]) -> None:
        squares = self.squares
        color = self.side
        enemy = color ^ 1
        if color == WHITE:
            king_sq, kingside, queenside = E1, WHITE_KINGSIDE, WHITE_QUEENSIDE
            rook_h, rook_a = H1, A1
            f_sq, g_sq, d_sq, c_sq, b_sq = F1, G1, D1, C1, B1
        else:
            king_sq, kingside, queenside = E8, BLACK_KINGSIDE, BLACK_QUEENSIDE
            rook_h, rook_a = H8, A8
            f_sq, g_sq, d_sq, c_sq, b_sq = F8, G8, D8, C8, B8
        king = make_piece(color, KING)
        rook = make_piece(color, ROOK)
        if squares[king_sq] != king:
            return
        if self.is_attacked(king_sq, enemy):
            return
        if (
            self.castling & kingside
            and squares[rook_h] == rook
            and squares[f_sq] == EMPTY
            and squares[g_sq] == EMPTY
            and not self.is_attacked(f_sq, enemy)
        ):
            moves.append(Move(king_sq, g_sq, 0, FLAG_CASTLE))
        if (
            self.castling & queenside
            and squares[rook_a] == rook
            and squares[d_sq] == EMPTY
            and squares[c_sq] == EMPTY
            and squares[b_sq] == EMPTY
            and not self.is_attacked(d_sq, enemy)
        ):
            moves.append(Move(king_sq, c_sq, 0, FLAG_CASTLE))

    def generate_legal_moves(self, captures_only: bool = False) -> list[Move]:
        legal: list[Move] = []
        color = self.side
        for move in self.generate_pseudo_legal(captures_only):
            self.make_move(move)
            if not self.is_attacked(self.king_square[color], color ^ 1):
                legal.append(move)
            self.unmake_move()
        return legal

    def make_move(self, move: Move) -> None:
        squares = self.squares
        color = self.side
        frm = move.frm
        to = move.to
        flags = move.flags
        piece = squares[frm]
        key = self.key

        if self.ep_square >= 0:
            key ^= EP_FILE_KEYS[file_of(self.ep_square)]
        key ^= CASTLING_KEYS[self.castling]

        captured = EMPTY
        captured_square = -1
        if flags & FLAG_EN_PASSANT:
            captured_square = to - 16 if color == WHITE else to + 16
            captured = squares[captured_square]
            squares[captured_square] = EMPTY
            key ^= PIECE_KEYS[captured][captured_square]
        elif squares[to] != EMPTY:
            captured_square = to
            captured = squares[to]
            key ^= PIECE_KEYS[captured][to]

        self.history.append(
            Undo(
                move,
                captured,
                captured_square,
                self.castling,
                self.ep_square,
                self.halfmove,
                self.key,
            )
        )

        squares[frm] = EMPTY
        key ^= PIECE_KEYS[piece][frm]
        moved = piece
        if flags & FLAG_PROMOTION:
            moved = make_piece(color, move.promo)
        squares[to] = moved
        key ^= PIECE_KEYS[moved][to]

        if flags & FLAG_CASTLE:
            rook_from, rook_to = _CASTLE_ROOK[to]
            rook_piece = squares[rook_from]
            squares[rook_from] = EMPTY
            squares[rook_to] = rook_piece
            key ^= PIECE_KEYS[rook_piece][rook_from]
            key ^= PIECE_KEYS[rook_piece][rook_to]

        if piece_type(piece) == KING:
            self.king_square[color] = to

        self.castling &= _CASTLE_MASK[frm] & _CASTLE_MASK[to]
        key ^= CASTLING_KEYS[self.castling]

        if flags & FLAG_DOUBLE_PUSH:
            self.ep_square = frm + (16 if color == WHITE else -16)
            key ^= EP_FILE_KEYS[file_of(self.ep_square)]
        else:
            self.ep_square = -1

        if piece_type(piece) == PAWN or captured != EMPTY:
            self.halfmove = 0
        else:
            self.halfmove += 1

        if color == BLACK:
            self.fullmove += 1

        self.side = color ^ 1
        key ^= SIDE_KEY
        self.key = key

    def unmake_move(self) -> None:
        undo = self.history.pop()
        move = undo.move
        squares = self.squares
        color = self.side ^ 1
        frm = move.frm
        to = move.to
        flags = move.flags

        moved = squares[to]
        squares[to] = EMPTY
        if flags & FLAG_PROMOTION:
            squares[frm] = make_piece(color, PAWN)
        else:
            squares[frm] = moved

        if flags & FLAG_CASTLE:
            rook_from, rook_to = _CASTLE_ROOK[to]
            squares[rook_from] = squares[rook_to]
            squares[rook_to] = EMPTY

        if undo.captured != EMPTY:
            squares[undo.captured_square] = undo.captured

        if piece_type(squares[frm]) == KING:
            self.king_square[color] = frm

        self.castling = undo.castling
        self.ep_square = undo.ep_square
        self.halfmove = undo.halfmove
        self.key = undo.key
        if color == BLACK:
            self.fullmove -= 1
        self.side = color

    def is_checkmate(self) -> bool:
        return self.in_check() and not self.generate_legal_moves()

    def is_stalemate(self) -> bool:
        return not self.in_check() and not self.generate_legal_moves()

    def find_move(self, text: str) -> Move | None:
        for move in self.generate_legal_moves():
            coord = square_name(move.frm) + square_name(move.to)
            if move.promo:
                coord += "nbrq"[(move.promo - KNIGHT)]
            if coord == text:
                return move
        return None

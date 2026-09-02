from __future__ import annotations

import random
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from chess import (
    BISHOP,
    BLACK,
    Board,
    KNIGHT,
    MATE_THRESHOLD,
    MATE_VALUE,
    QUEEN,
    ROOK,
    START_FEN,
    Searcher,
    WHITE,
    move_to_san,
    move_to_uci,
    parse_square,
    perft,
)
from chess.pieces import C8, FLAG_CASTLE, FLAG_EN_PASSANT

KIWIPETE = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"
POSITION_3 = "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"
POSITION_4 = "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1"
POSITION_5 = "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8"
POSITION_6 = "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10"


def uci_moves(board: Board) -> set[str]:
    return {move_to_uci(move) for move in board.generate_legal_moves()}


class TestFen(unittest.TestCase):
    def test_round_trip(self) -> None:
        for fen in (
            START_FEN,
            KIWIPETE,
            POSITION_3,
            POSITION_4,
            POSITION_5,
            POSITION_6,
            "4k3/8/8/8/8/8/8/4K3 w - - 0 1",
            "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3",
        ):
            self.assertEqual(Board(fen).fen(), fen)

    def test_fields_parsed(self) -> None:
        board = Board("rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 7 12")
        self.assertEqual(board.side, WHITE)
        self.assertEqual(board.ep_square, parse_square("f6"))
        self.assertEqual(board.halfmove, 7)
        self.assertEqual(board.fullmove, 12)
        self.assertEqual(board.castling, 15)

    def test_black_to_move_and_no_rights(self) -> None:
        board = Board("8/8/8/4k3/8/8/8/4K3 b - - 3 9")
        self.assertEqual(board.side, BLACK)
        self.assertEqual(board.castling, 0)
        self.assertEqual(board.ep_square, -1)
        self.assertEqual(board.fen(), "8/8/8/4k3/8/8/8/4K3 b - - 3 9")


class TestPerft(unittest.TestCase):
    def test_starting_position(self) -> None:
        board = Board(START_FEN)
        for depth, expected in enumerate([20, 400, 8902, 197281], start=1):
            self.assertEqual(perft(board, depth), expected, f"startpos depth {depth}")

    def test_kiwipete(self) -> None:
        board = Board(KIWIPETE)
        for depth, expected in enumerate([48, 2039, 97862], start=1):
            self.assertEqual(perft(board, depth), expected, f"kiwipete depth {depth}")

    def test_position_3(self) -> None:
        board = Board(POSITION_3)
        for depth, expected in enumerate([14, 191, 2812, 43238], start=1):
            self.assertEqual(perft(board, depth), expected, f"position 3 depth {depth}")

    def test_position_4_promotions(self) -> None:
        board = Board(POSITION_4)
        for depth, expected in enumerate([6, 264, 9467], start=1):
            self.assertEqual(perft(board, depth), expected, f"position 4 depth {depth}")

    def test_position_5(self) -> None:
        board = Board(POSITION_5)
        for depth, expected in enumerate([44, 1486, 62379], start=1):
            self.assertEqual(perft(board, depth), expected, f"position 5 depth {depth}")

    def test_position_6(self) -> None:
        board = Board(POSITION_6)
        for depth, expected in enumerate([46, 2079, 89890], start=1):
            self.assertEqual(perft(board, depth), expected, f"position 6 depth {depth}")

    def test_board_restored_after_perft(self) -> None:
        board = Board(KIWIPETE)
        perft(board, 3)
        self.assertEqual(board.fen(), KIWIPETE)
        self.assertEqual(board.history, [])


class TestCastling(unittest.TestCase):
    def test_both_sides_available(self) -> None:
        moves = uci_moves(Board("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"))
        self.assertIn("e1g1", moves)
        self.assertIn("e1c1", moves)

    def test_black_both_sides_available(self) -> None:
        moves = uci_moves(Board("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1"))
        self.assertIn("e8g8", moves)
        self.assertIn("e8c8", moves)

    def test_blocked_by_own_piece(self) -> None:
        moves = uci_moves(Board("r3k2r/8/8/8/8/8/8/R2QK1NR w KQkq - 0 1"))
        self.assertNotIn("e1g1", moves)
        self.assertNotIn("e1c1", moves)

    def test_b1_occupied_still_allows_queenside(self) -> None:
        moves = uci_moves(Board("r3k2r/8/8/8/8/8/8/RN2K2R w KQkq - 0 1"))
        self.assertNotIn("e1c1", moves)

    def test_through_check_is_illegal(self) -> None:
        moves = uci_moves(Board("r3k2r/8/8/8/8/8/6b1/R3K2R w KQkq - 0 1"))
        self.assertNotIn("e1g1", moves)
        self.assertIn("e1c1", moves)

    def test_destination_attacked_is_illegal(self) -> None:
        moves = uci_moves(Board("r3k2r/8/8/8/8/8/8/R3K1nR w KQkq - 0 1"))
        self.assertNotIn("e1g1", moves)

    def test_out_of_check_is_illegal(self) -> None:
        moves = uci_moves(Board("r3k2r/8/8/8/8/8/4r3/R3K2R w KQkq - 0 1"))
        self.assertNotIn("e1g1", moves)
        self.assertNotIn("e1c1", moves)

    def test_rights_lost_after_rook_move(self) -> None:
        board = Board("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1")
        board.make_move(board.find_move("h1g1"))
        board.make_move(board.find_move("a8b8"))
        self.assertNotIn("K", board.fen().split()[2])
        self.assertIn("Q", board.fen().split()[2])
        self.assertNotIn("q", board.fen().split()[2])
        self.assertIn("k", board.fen().split()[2])

    def test_rights_lost_after_king_move(self) -> None:
        board = Board("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1")
        board.make_move(board.find_move("e1f1"))
        self.assertEqual(board.fen().split()[2], "kq")

    def test_rights_lost_when_rook_captured(self) -> None:
        board = Board("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1")
        board.make_move(board.find_move("a1a8"))
        self.assertEqual(board.fen().split()[2], "Kk")

    def test_rook_relocates_on_castle_and_unmakes(self) -> None:
        fen = "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"
        board = Board(fen)
        move = board.find_move("e1c1")
        self.assertTrue(move.flags & FLAG_CASTLE)
        board.make_move(move)
        self.assertEqual(board.fen().split()[0].split("/")[-1], "2KR3R")
        board.unmake_move()
        self.assertEqual(board.fen(), fen)


class TestEnPassant(unittest.TestCase):
    def test_double_push_sets_target(self) -> None:
        board = Board(START_FEN)
        board.make_move(board.find_move("e2e4"))
        self.assertEqual(board.ep_square, parse_square("e3"))
        board.unmake_move()
        self.assertEqual(board.ep_square, -1)

    def test_capture_is_generated_and_removes_pawn(self) -> None:
        board = Board("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2")
        move = board.find_move("e5d6")
        self.assertIsNotNone(move)
        self.assertTrue(move.flags & FLAG_EN_PASSANT)
        board.make_move(move)
        self.assertEqual(board.fen().split()[0], "4k3/8/3P4/8/8/8/8/4K3")
        board.unmake_move()
        self.assertEqual(board.fen(), "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2")

    def test_illegal_due_to_discovered_check(self) -> None:
        board = Board("8/8/8/8/k1p4R/8/3P4/3K4 w - - 0 1")
        board.make_move(board.find_move("d2d4"))
        self.assertEqual(board.ep_square, parse_square("d3"))
        pseudo = {move_to_uci(m) for m in board.generate_pseudo_legal()}
        self.assertIn("c4d3", pseudo)
        self.assertNotIn("c4d3", uci_moves(board))

    def test_legal_when_pin_line_is_not_exposed(self) -> None:
        board = Board("8/8/8/8/k1p5/8/3P4/3K3R w - - 0 1")
        board.make_move(board.find_move("d2d4"))
        self.assertIn("c4d3", uci_moves(board))

    def test_target_cleared_after_quiet_move(self) -> None:
        board = Board(START_FEN)
        board.make_move(board.find_move("e2e4"))
        board.make_move(board.find_move("b8c6"))
        self.assertEqual(board.ep_square, -1)


class TestPromotion(unittest.TestCase):
    def test_all_four_pieces_on_push(self) -> None:
        board = Board("8/P7/8/8/8/8/8/K6k w - - 0 1")
        promos = {m.promo for m in board.generate_legal_moves() if m.to == parse_square("a8")}
        self.assertEqual(promos, {QUEEN, ROOK, BISHOP, KNIGHT})

    def test_all_four_pieces_on_capture(self) -> None:
        board = Board("1n6/P7/8/8/8/8/8/K6k w - - 0 1")
        targets = {
            (move_to_uci(m))
            for m in board.generate_legal_moves()
            if m.frm == parse_square("a7")
        }
        self.assertEqual(
            targets,
            {"a7a8q", "a7a8r", "a7a8b", "a7a8n", "a7b8q", "a7b8r", "a7b8b", "a7b8n"},
        )

    def test_promoted_piece_appears_and_reverts(self) -> None:
        board = Board("8/P7/8/8/8/8/8/K6k w - - 0 1")
        move = board.find_move("a7a8n")
        board.make_move(move)
        self.assertEqual(board.fen().split()[0], "N7/8/8/8/8/8/8/K6k")
        board.unmake_move()
        self.assertEqual(board.fen(), "8/P7/8/8/8/8/8/K6k w - - 0 1")

    def test_black_promotion_underpromotes_to_knight_with_check(self) -> None:
        board = Board("8/8/8/8/8/8/5p2/4K2k b - - 0 1")
        self.assertIn("f2f1n", uci_moves(board))


class TestGameEnd(unittest.TestCase):
    def test_fools_mate(self) -> None:
        board = Board("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3")
        self.assertTrue(board.in_check())
        self.assertTrue(board.is_checkmate())
        self.assertFalse(board.is_stalemate())
        self.assertEqual(board.generate_legal_moves(), [])

    def test_back_rank_mate(self) -> None:
        board = Board("6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1")
        board.make_move(board.find_move("a1a8"))
        self.assertTrue(board.is_checkmate())

    def test_stalemate(self) -> None:
        board = Board("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1")
        self.assertFalse(board.in_check())
        self.assertTrue(board.is_stalemate())
        self.assertFalse(board.is_checkmate())

    def test_stalemate_with_blocked_pawn(self) -> None:
        board = Board("8/8/8/8/8/5k2/5p2/5K2 w - - 0 1")
        self.assertTrue(board.is_stalemate())

    def test_check_but_not_mate(self) -> None:
        board = Board("4k2R/8/8/8/8/8/8/4K3 b - - 0 1")
        self.assertTrue(board.in_check())
        self.assertFalse(board.is_checkmate())

    def test_only_evasions_are_generated_in_check(self) -> None:
        board = Board("4k3/8/8/8/8/8/4r3/4K3 w - - 0 1")
        self.assertTrue(board.in_check())
        for move in board.generate_legal_moves():
            board.make_move(move)
            self.assertFalse(board.is_attacked(board.king_square[WHITE], BLACK))
            board.unmake_move()


class TestZobrist(unittest.TestCase):
    def test_transposition_gives_same_key(self) -> None:
        first = Board(START_FEN)
        for uci in ("g1f3", "g8f6", "b1c3", "b8c6"):
            first.make_move(first.find_move(uci))
        second = Board(START_FEN)
        for uci in ("b1c3", "b8c6", "g1f3", "g8f6"):
            second.make_move(second.find_move(uci))
        self.assertEqual(first.fen(), second.fen())
        self.assertEqual(first.key, second.key)

    def test_different_positions_differ(self) -> None:
        board = Board(START_FEN)
        start_key = board.key
        board.make_move(board.find_move("e2e4"))
        self.assertNotEqual(board.key, start_key)

    def test_side_to_move_is_part_of_key(self) -> None:
        white = Board("4k3/8/8/8/8/8/8/4K3 w - - 0 1")
        black = Board("4k3/8/8/8/8/8/8/4K3 b - - 0 1")
        self.assertNotEqual(white.key, black.key)

    def test_castling_rights_are_part_of_key(self) -> None:
        full = Board("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1")
        none = Board("r3k2r/8/8/8/8/8/8/R3K2R w - - 0 1")
        self.assertNotEqual(full.key, none.key)

    def test_key_restored_after_unmake(self) -> None:
        for fen in (START_FEN, KIWIPETE, POSITION_4, POSITION_5):
            board = Board(fen)
            original = board.key
            for move in board.generate_legal_moves():
                board.make_move(move)
                board.unmake_move()
                self.assertEqual(board.key, original)
                self.assertEqual(board.fen(), fen)

    def test_incremental_key_matches_full_recompute(self) -> None:
        rng = random.Random(7)
        for fen in (START_FEN, KIWIPETE, POSITION_4):
            board = Board(fen)
            for _ in range(60):
                moves = board.generate_legal_moves()
                if not moves:
                    break
                board.make_move(rng.choice(moves))
                self.assertEqual(board.key, board.compute_key())
            while board.history:
                board.unmake_move()
                self.assertEqual(board.key, board.compute_key())
            self.assertEqual(board.fen(), fen)


class TestSearch(unittest.TestCase):
    def test_finds_mate_in_two_with_rook_sacrifice(self) -> None:
        board = Board("r5k1/5ppp/8/8/8/8/2R2PPP/2R3K1 w - - 0 1")
        result = Searcher().search(board, 4)
        self.assertEqual(result.move.to, C8)
        self.assertGreater(result.score, MATE_THRESHOLD)
        self.assertEqual(MATE_VALUE - result.score, 3)
        self.assertEqual(len(result.pv), 3)

    def test_finds_mate_in_two_in_king_and_rook_ending(self) -> None:
        board = Board("k7/8/2K5/8/8/8/8/7R w - - 0 1")
        result = Searcher().search(board, 4)
        self.assertGreater(result.score, MATE_THRESHOLD)
        self.assertEqual(MATE_VALUE - result.score, 3)

    def test_finds_known_winning_queen_sacrifice(self) -> None:
        board = Board("2rr3k/pp3pp1/1nnqbN1p/3pN3/2pP4/2P3Q1/PPB4P/R4RK1 w - - 0 1")
        result = Searcher().search(board, 4)
        self.assertEqual(move_to_san(board, result.move), "Qg6")
        self.assertGreater(result.score, MATE_THRESHOLD)

    def test_takes_free_material(self) -> None:
        board = Board("4k3/8/8/3q4/4P3/8/8/4K3 w - - 0 1")
        result = Searcher().search(board, 3)
        self.assertEqual(move_to_uci(result.move), "e4d5")

    def test_avoids_stalemate_score_confusion(self) -> None:
        board = Board("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1")
        result = Searcher().search(board, 3)
        self.assertIsNone(result.move)
        self.assertEqual(result.score, 0)

    def test_transposition_table_matches_plain_search(self) -> None:
        board = Board("r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4")
        plain = Searcher(use_transposition_table=False, use_move_ordering=False)
        tuned = Searcher()
        plain_result = plain.search(board.clone(), 3)
        tuned_result = tuned.search(board.clone(), 3)
        self.assertEqual(plain_result.score, tuned_result.score)
        self.assertLess(tuned_result.nodes, plain_result.nodes)

    def test_board_is_unchanged_by_search(self) -> None:
        fen = KIWIPETE
        board = Board(fen)
        Searcher().search(board, 3)
        self.assertEqual(board.fen(), fen)
        self.assertEqual(board.history, [])


class TestNotation(unittest.TestCase):
    def test_basic_san(self) -> None:
        board = Board(START_FEN)
        self.assertEqual(move_to_san(board, board.find_move("e2e4")), "e4")
        self.assertEqual(move_to_san(board, board.find_move("g1f3")), "Nf3")

    def test_castling_san(self) -> None:
        board = Board("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1")
        self.assertEqual(move_to_san(board, board.find_move("e1g1")), "O-O")
        self.assertEqual(move_to_san(board, board.find_move("e1c1")), "O-O-O")

    def test_capture_and_promotion_san(self) -> None:
        board = Board("1n6/P7/8/8/8/8/8/K6k w - - 0 1")
        self.assertEqual(move_to_san(board, board.find_move("a7b8q")), "axb8=Q")

    def test_disambiguation_by_file(self) -> None:
        board = Board("4k3/8/8/8/4K3/8/8/R6R w - - 0 1")
        self.assertEqual(move_to_san(board, board.find_move("a1c1")), "Rac1")
        self.assertEqual(move_to_san(board, board.find_move("h1c1")), "Rhc1")

    def test_disambiguation_by_rank(self) -> None:
        board = Board("4k3/8/8/8/R7/8/8/R3K3 w - - 0 1")
        self.assertEqual(move_to_san(board, board.find_move("a1a2")), "R1a2")

    def test_mate_suffix(self) -> None:
        board = Board("6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1")
        self.assertEqual(move_to_san(board, board.find_move("a1a8")), "Ra8#")

    def test_san_does_not_mutate_board(self) -> None:
        board = Board(KIWIPETE)
        for move in board.generate_legal_moves():
            move_to_san(board, move)
        self.assertEqual(board.fen(), KIWIPETE)


if __name__ == "__main__":
    unittest.main()

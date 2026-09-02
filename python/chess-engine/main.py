from __future__ import annotations

import time

from chess import (
    Board,
    START_FEN,
    Searcher,
    WHITE,
    material_balance,
    move_to_san,
    perft,
    san_line,
    score_text,
)

KIWIPETE = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"
ITALIAN = "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4"

PERFT_CASES = [
    ("starting position", START_FEN, [20, 400, 8902, 197281]),
    ("Kiwipete", KIWIPETE, [48, 2039, 97862]),
    ("promotion maze", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", [6, 264, 9467]),
]

TACTICS = [
    (
        "a queen offer on g6 that forces mate in two",
        "2rr3k/pp3pp1/1nnqbN1p/3pN3/2pP4/2P3Q1/PPB4P/R4RK1 w - - 0 1",
        5,
        "Qg6",
    ),
    (
        "back-rank mate in two after a rook deflection",
        "r5k1/5ppp/8/8/8/8/2R2PPP/2R3K1 w - - 0 1",
        4,
        "Rc8+",
    ),
    (
        "a rook lift that harasses the queen and nets material",
        "5rk1/1ppb3p/p1pb4/6q1/3P1p1r/2P1R2P/PP1BQ1P1/5RKN w - - 0 1",
        5,
        "Rg3",
    ),
]


def banner(title: str) -> None:
    print()
    print("=" * 68)
    print(title)
    print("=" * 68)


def show_board() -> None:
    banner("1. Board representation and FEN")
    board = Board(START_FEN)
    print(board)
    print()
    print("FEN in :", START_FEN)
    print("FEN out:", board.fen())
    print("round trip identical:", board.fen() == START_FEN)
    print("zobrist key:", hex(board.key))
    board.make_move(board.find_move("e2e4"))
    print()
    print("after 1. e4 the en passant target and the key both change")
    print("FEN out:", board.fen())
    print("zobrist key:", hex(board.key))
    board.unmake_move()
    print("after unmake, key is restored:", hex(board.key))


def show_perft() -> None:
    banner("2. Perft: proving the move generator against published counts")
    for name, fen, expected in PERFT_CASES:
        board = Board(fen)
        print(f"{name}")
        print(f"  {fen}")
        for depth, want in enumerate(expected, start=1):
            start = time.perf_counter()
            got = perft(board, depth)
            elapsed = time.perf_counter() - start
            verdict = "ok" if got == want else "MISMATCH"
            print(
                f"  depth {depth}: {got:>9,} nodes  expected {want:>9,}  "
                f"{verdict}  ({elapsed:.2f}s)"
            )
        print()


def show_tactics() -> None:
    banner("3. Tactics: does the search find the known move?")
    for description, fen, depth, expected in TACTICS:
        board = Board(fen)
        result = Searcher().search(board, depth)
        played = move_to_san(board, result.move)
        print(description)
        print(f"  {fen}")
        print(
            f"  depth {result.depth} -> {played} ({score_text(result.score)}), "
            f"expected {expected}, {'ok' if played == expected else 'MISMATCH'}"
        )
        print(f"  pv: {san_line(board, result.pv)}")
        print(
            f"  {result.nodes:,} search nodes + {result.quiescence_nodes:,} "
            f"quiescence nodes in {result.elapsed:.2f}s"
        )
        print()


def show_search_economy() -> None:
    banner("4. What the transposition table and move ordering are worth")
    print("Same position, same depth, same score - only the pruning differs.")
    print("Quiescence is switched off here so both columns count the same tree.")
    print()
    print(f"  {ITALIAN}")
    print()
    header = f"  {'depth':>5}  {'plain alpha-beta':>18}  {'TT + ordering':>15}  {'ratio':>7}  {'agree':>6}"
    print(header)
    print("  " + "-" * (len(header) - 2))
    for depth in (3, 4, 5):
        plain = Searcher(
            use_transposition_table=False,
            use_move_ordering=False,
            use_quiescence=False,
        ).search(Board(ITALIAN), depth)
        tuned = Searcher(use_quiescence=False).search(Board(ITALIAN), depth)
        ratio = plain.nodes / tuned.nodes
        agree = plain.score == tuned.score
        print(
            f"  {depth:>5}  {plain.nodes:>18,}  {tuned.nodes:>15,}  "
            f"{ratio:>6.1f}x  {str(agree):>6}"
        )
    print()
    print("Node counts are raw negamax visits; both searches return the same value,")
    print("so the reduction is pure pruning rather than a shallower search.")


def show_self_play(plies: int = 16, depth: int = 3) -> None:
    banner("5. Self play")
    board = Board(START_FEN)
    searcher = Searcher()
    line: list[str] = []
    total_nodes = 0
    for _ in range(plies):
        moves = board.generate_legal_moves()
        if not moves:
            break
        result = searcher.search(board, depth)
        if result.move is None:
            break
        total_nodes += result.nodes + result.quiescence_nodes
        san = move_to_san(board, result.move)
        if board.side == WHITE:
            line.append(f"{board.fullmove}. {san}")
        else:
            line.append(san)
        board.make_move(result.move)
    print(" ".join(line))
    print()
    print(board)
    print()
    if board.is_checkmate():
        print("result: checkmate")
    elif board.is_stalemate():
        print("result: stalemate")
    else:
        print(f"material balance (white minus black, centipawns): {material_balance(board)}")
    print(f"searched {total_nodes:,} nodes at depth {depth} across {len(line)} plies")
    print("final FEN:", board.fen())


def main() -> None:
    print("chess-engine: 0x88 board, legal move generation, alpha-beta search")
    show_board()
    show_perft()
    show_tactics()
    show_search_economy()
    show_self_play()
    print()


if __name__ == "__main__":
    main()

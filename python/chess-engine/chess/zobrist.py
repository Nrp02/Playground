from __future__ import annotations

import random

_rng = random.Random(0x5EED_C4E5)

PIECE_KEYS: list[list[int]] = [
    [_rng.getrandbits(64) for _ in range(128)] for _ in range(16)
]
SIDE_KEY: int = _rng.getrandbits(64)
CASTLING_KEYS: list[int] = [_rng.getrandbits(64) for _ in range(16)]
EP_FILE_KEYS: list[int] = [_rng.getrandbits(64) for _ in range(8)]

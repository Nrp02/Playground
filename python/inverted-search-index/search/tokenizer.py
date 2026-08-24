from __future__ import annotations

import re
from typing import List

_TOKEN_PATTERN = re.compile(r"[a-z0-9]+")

STOPWORDS = frozenset(
    {
        "a", "an", "the", "and", "or", "but", "if", "then", "of", "to",
        "in", "on", "at", "by", "for", "with", "about", "as", "into",
        "is", "are", "was", "were", "be", "been", "being", "it", "its",
        "this", "that", "these", "those", "from", "not", "no",
    }
)


def tokenize(text: str, remove_stopwords: bool = True) -> List[str]:
    lowered = text.lower()
    tokens = _TOKEN_PATTERN.findall(lowered)
    if remove_stopwords:
        tokens = [token for token in tokens if token not in STOPWORDS]
    return tokens

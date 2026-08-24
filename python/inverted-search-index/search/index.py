from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

from search.tokenizer import tokenize


@dataclass
class Document:
    doc_id: int
    text: str
    term_counts: Dict[str, int] = field(default_factory=dict)
    length: int = 0


class InvertedIndex:
    def __init__(self) -> None:
        self.documents: Dict[int, Document] = {}
        self.postings: Dict[str, Dict[int, int]] = {}
        self._idf_cache: Dict[str, float] = {}
        self._next_doc_id = 0

    def __len__(self) -> int:
        return len(self.documents)

    def add_document(self, text: str, doc_id: Optional[int] = None) -> int:
        if doc_id is None:
            doc_id = self._next_doc_id
        self._next_doc_id = max(self._next_doc_id, doc_id + 1)

        tokens = tokenize(text)
        term_counts: Dict[str, int] = {}
        for token in tokens:
            term_counts[token] = term_counts.get(token, 0) + 1

        self.documents[doc_id] = Document(
            doc_id=doc_id, text=text, term_counts=term_counts, length=len(tokens)
        )
        for term, count in term_counts.items():
            self.postings.setdefault(term, {})[doc_id] = count

        self.recompute_idf()
        return doc_id

    def recompute_idf(self) -> None:
        n = len(self.documents)
        self._idf_cache = {}
        if n == 0:
            return
        for term, docs in self.postings.items():
            df = len(docs)
            self._idf_cache[term] = math.log(n / df)

    def term_frequency(self, term: str, doc_id: int) -> int:
        return self.postings.get(term, {}).get(doc_id, 0)

    def document_frequency(self, term: str) -> int:
        return len(self.postings.get(term, {}))

    def inverse_document_frequency(self, term: str) -> float:
        return self._idf_cache.get(term, 0.0)

    def tfidf(self, term: str, doc_id: int) -> float:
        tf = self.term_frequency(term, doc_id)
        if tf == 0:
            return 0.0
        return tf * self.inverse_document_frequency(term)

    def get_document(self, doc_id: int) -> Optional[Document]:
        return self.documents.get(doc_id)

    def query(self, query_text: str, top_k: int = 5) -> List[Tuple[int, float]]:
        query_terms = tokenize(query_text)
        scores: Dict[int, float] = {}
        for term in query_terms:
            postings = self.postings.get(term)
            if not postings:
                continue
            idf = self.inverse_document_frequency(term)
            for doc_id, tf in postings.items():
                scores[doc_id] = scores.get(doc_id, 0.0) + tf * idf
        ranked = sorted(scores.items(), key=lambda item: (-item[1], item[0]))
        return ranked[:top_k]

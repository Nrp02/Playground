import math
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from search.index import InvertedIndex
from search.tokenizer import tokenize


class TestTokenizer(unittest.TestCase):
    def test_lowercases_and_strips_punctuation(self):
        tokens = tokenize("Hello, World! This is a TEST.", remove_stopwords=False)
        self.assertEqual(tokens, ["hello", "world", "this", "is", "a", "test"])

    def test_removes_stopwords_by_default(self):
        tokens = tokenize("The cat and the dog are friends")
        self.assertEqual(tokens, ["cat", "dog", "friends"])

    def test_stopword_removal_can_be_disabled(self):
        tokens = tokenize("The cat and the dog are friends", remove_stopwords=False)
        self.assertIn("the", tokens)
        self.assertIn("and", tokens)

    def test_handles_numbers_and_mixed_punctuation(self):
        tokens = tokenize("Room 42B, aisle-7... go!", remove_stopwords=False)
        self.assertEqual(tokens, ["room", "42b", "aisle", "7", "go"])

    def test_empty_string_yields_no_tokens(self):
        self.assertEqual(tokenize(""), [])


class TestPostings(unittest.TestCase):
    def test_postings_track_term_frequency_per_document(self):
        index = InvertedIndex()
        index.add_document("apple banana apple")
        index.add_document("banana banana cherry")
        self.assertEqual(index.postings["apple"], {0: 2})
        self.assertEqual(index.postings["banana"], {0: 1, 1: 2})
        self.assertEqual(index.postings["cherry"], {1: 1})

    def test_term_frequency_helper_matches_postings(self):
        index = InvertedIndex()
        index.add_document("apple banana apple")
        self.assertEqual(index.term_frequency("apple", 0), 2)
        self.assertEqual(index.term_frequency("missing", 0), 0)
        self.assertEqual(index.term_frequency("apple", 999), 0)

    def test_document_frequency(self):
        index = InvertedIndex()
        index.add_document("apple banana")
        index.add_document("apple cherry")
        index.add_document("cherry date")
        self.assertEqual(index.document_frequency("apple"), 2)
        self.assertEqual(index.document_frequency("cherry"), 2)
        self.assertEqual(index.document_frequency("date"), 1)


class TestTfIdfScoring(unittest.TestCase):
    def setUp(self):
        self.index = InvertedIndex()
        self.index.add_document("cat sat mat")
        self.index.add_document("cat sat hat")
        self.index.add_document("dog barked loudly")

    def test_idf_matches_hand_computed_values(self):
        expected_idf_cat = math.log(3 / 2)
        expected_idf_mat = math.log(3 / 1)
        self.assertAlmostEqual(self.index.inverse_document_frequency("cat"), expected_idf_cat)
        self.assertAlmostEqual(self.index.inverse_document_frequency("mat"), expected_idf_mat)

    def test_tfidf_matches_hand_computed_values(self):
        expected_idf_cat = math.log(3 / 2)
        self.assertAlmostEqual(self.index.tfidf("cat", 0), 1 * expected_idf_cat)
        self.assertEqual(self.index.tfidf("dog", 0), 0.0)

    def test_tfidf_scales_with_term_frequency(self):
        index = InvertedIndex()
        index.add_document("cat cat cat dog")
        index.add_document("dog")
        idf_cat = math.log(2 / 1)
        self.assertAlmostEqual(index.tfidf("cat", 0), 3 * idf_cat)


class TestRankedQuery(unittest.TestCase):
    def setUp(self):
        self.index = InvertedIndex()
        self.index.add_document("cat sat mat")
        self.index.add_document("cat sat hat")
        self.index.add_document("dog barked loudly")

    def test_query_ranks_by_summed_tfidf(self):
        results = self.index.query("cat mat")
        self.assertEqual([doc_id for doc_id, _ in results], [0, 1])
        idf_cat = math.log(3 / 2)
        idf_mat = math.log(3 / 1)
        expected_doc0_score = idf_cat + idf_mat
        expected_doc1_score = idf_cat
        self.assertAlmostEqual(results[0][1], expected_doc0_score)
        self.assertAlmostEqual(results[1][1], expected_doc1_score)

    def test_documents_with_no_matching_terms_are_excluded(self):
        results = self.index.query("mat")
        doc_ids = [doc_id for doc_id, _ in results]
        self.assertIn(0, doc_ids)
        self.assertNotIn(2, doc_ids)

    def test_top_k_limits_result_count(self):
        results = self.index.query("cat sat mat hat", top_k=1)
        self.assertEqual(len(results), 1)

    def test_unknown_query_terms_yield_no_results(self):
        results = self.index.query("spaceship rocket")
        self.assertEqual(results, [])


class TestIncrementalAddition(unittest.TestCase):
    def test_new_document_becomes_searchable_immediately(self):
        index = InvertedIndex()
        index.add_document("cat sat mat")
        self.assertEqual(index.query("wizard"), [])
        new_id = index.add_document("a wizard cast a spell")
        results = index.query("wizard")
        self.assertEqual([doc_id for doc_id, _ in results], [new_id])

    def test_idf_is_recomputed_as_documents_are_added(self):
        index = InvertedIndex()
        index.add_document("cat sat mat")
        index.add_document("cat sat hat")
        idf_before = index.inverse_document_frequency("cat")
        self.assertAlmostEqual(idf_before, 0.0)
        index.add_document("dog barked loudly")
        idf_after = index.inverse_document_frequency("cat")
        self.assertAlmostEqual(idf_after, math.log(3 / 2))
        self.assertGreater(idf_after, idf_before)

    def test_incremental_addition_changes_subsequent_query_ranking(self):
        index = InvertedIndex()
        index.add_document("cat sat mat")
        index.add_document("dog barked loudly")
        first_results = index.query("dog")
        self.assertEqual(first_results[0][0], 1)

        new_id = index.add_document("dog dog dog barked loudly at the mailman")

        second_results = index.query("dog")
        self.assertEqual(second_results[0][0], new_id)
        self.assertGreater(second_results[0][1], first_results[0][1])


if __name__ == "__main__":
    unittest.main()

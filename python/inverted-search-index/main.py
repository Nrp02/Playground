from search.index import InvertedIndex

DOCUMENTS = [
    "The cat sat on the mat and watched birds outside the window",
    "A dog barked loudly at the mail carrier walking down the street",
    "Python is a popular programming language for data science and scripting",
    "The chef roasted vegetables and grilled salmon for the dinner special",
    "Astronomers discovered a distant galaxy using a powerful new telescope",
    "The stock market rallied after the central bank cut interest rates",
    "She practiced piano scales every morning before school started",
    "Rust and C++ are both compiled languages used in systems programming",
    "The hikers reached the mountain summit just before sunset",
    "A new vaccine trial showed promising results against the virus",
    "The soccer team celebrated their championship victory with the fans",
    "Electric cars are becoming more affordable as battery prices fall",
    "The novelist finished her fourth book about a family in wartime",
    "Rain flooded the streets after the storm passed over the coastal city",
    "Machine learning models require large datasets and careful tuning",
    "The museum opened a new exhibit featuring ancient Roman artifacts",
    "Investors watched the stock market closely during the earnings season",
    "The programming class covered loops functions and recursive algorithms",
]


def print_results(index: InvertedIndex, query: str, top_k: int = 3) -> None:
    print(f"\nquery: {query!r}")
    results = index.query(query, top_k=top_k)
    if not results:
        print("  no matches")
        return
    for doc_id, score in results:
        document = index.get_document(doc_id)
        text = document.text if document else ""
        print(f"  doc {doc_id:2d}  score={score:.4f}  {text}")


def main() -> int:
    index = InvertedIndex()
    for text in DOCUMENTS:
        index.add_document(text)

    print(f"indexed {len(index)} documents, vocabulary size {len(index.postings)}")

    for query in [
        "stock market interest rates",
        "programming language for data science",
        "mountain hikers sunset",
        "dog barking street",
    ]:
        print_results(index, query)

    print("\n=== adding a new document about programming and stock trading ===")
    new_doc_id = index.add_document(
        "A programmer built a trading bot in Python to track the stock market"
    )
    print(f"added doc {new_doc_id}, vocabulary size is now {len(index.postings)}")

    print_results(index, "stock market interest rates")
    print_results(index, "programming language for data science")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

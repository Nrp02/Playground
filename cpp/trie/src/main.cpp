#include <iostream>
#include <vector>

#include "trie.hpp"

namespace {

void printAutocomplete(const trie::Trie& t, const std::string& prefix, std::size_t k) {
    std::cout << "autocomplete(\"" << prefix << "\", " << k << ") = [";
    auto results = t.autocomplete(prefix, k);
    for (std::size_t i = 0; i < results.size(); ++i) {
        std::cout << results[i];
        if (i + 1 < results.size()) {
            std::cout << ", ";
        }
    }
    std::cout << "]\n";
}

}

int main() {
    trie::Trie t;

    std::vector<std::string> words = {
        "cat", "car", "care", "career", "careful", "cart", "cartoon",
        "dog", "dodge", "dodgy", "done", "door",
        "apple", "app", "apply", "application", "apricot",
        "banana", "band", "bandana", "bandwidth", "bank", "banker", "banking",
        "tree", "trie", "trigger", "trip", "triangle", "true", "truth", "try",
        "sun", "sunday", "sunflower", "sunset", "super", "supper",
        "go", "goat", "gold", "golf", "gone", "good", "goose",
    };

    for (const auto& word : words) {
        t.insert(word);
    }

    std::cout << "=== trie built ===\n";
    std::cout << "word count: " << t.wordCount() << "\n\n";

    std::cout << "=== search ===\n";
    for (const auto& word : {std::string("car"), std::string("care"), std::string("careful"),
                              std::string("carp"), std::string("go"), std::string("golfing")}) {
        std::cout << "search(\"" << word << "\") = " << std::boolalpha << t.search(word) << "\n";
    }

    std::cout << "\n=== startsWith ===\n";
    for (const auto& prefix : {std::string("car"), std::string("tri"), std::string("ban"),
                                std::string("xyz")}) {
        std::cout << "startsWith(\"" << prefix << "\") = " << std::boolalpha << t.startsWith(prefix)
                  << "\n";
    }

    std::cout << "\n=== autocomplete ===\n";
    printAutocomplete(t, "car", 10);
    printAutocomplete(t, "tri", 10);
    printAutocomplete(t, "ban", 3);
    printAutocomplete(t, "go", 10);
    printAutocomplete(t, "xyz", 5);

    std::cout << "\n=== erase demo ===\n";
    std::cout << "before erase: word count = " << t.wordCount() << ", search(\"car\") = "
              << std::boolalpha << t.search("car") << ", search(\"care\") = " << t.search("care")
              << "\n";
    bool erased = t.erase("car");
    std::cout << "erase(\"car\") = " << erased << "\n";
    std::cout << "after erase: word count = " << t.wordCount() << ", search(\"car\") = "
              << t.search("car") << ", search(\"care\") = " << t.search("care")
              << ", startsWith(\"car\") = " << t.startsWith("car") << "\n";
    std::cout << "erase(\"car\") again = " << t.erase("car") << "\n";
    std::cout << "erase(\"missing\") = " << t.erase("missing") << "\n";

    return 0;
}

use crate::trie::{Trie, TrieNode};

#[allow(dead_code)]
pub fn levenshtein(a: &str, b: &str) -> usize {
    let a: Vec<char> = a.chars().collect();
    let b: Vec<char> = b.chars().collect();
    let mut prev: Vec<usize> = (0..=b.len()).collect();
    let mut curr = vec![0usize; b.len() + 1];

    for i in 1..=a.len() {
        curr[0] = i;
        for j in 1..=b.len() {
            let cost = if a[i - 1] == b[j - 1] { 0 } else { 1 };
            curr[j] = (prev[j] + 1)
                .min(curr[j - 1] + 1)
                .min(prev[j - 1] + cost);
        }
        std::mem::swap(&mut prev, &mut curr);
    }
    prev[b.len()]
}

pub fn fuzzy_search(trie: &Trie, word: &str, max_dist: usize) -> Vec<(String, usize)> {
    let chars: Vec<char> = word.chars().collect();
    let first_row: Vec<usize> = (0..=chars.len()).collect();
    let mut results = Vec::new();
    for (ch, child) in trie.root.children.iter() {
        search_recursive(child, *ch, &chars, &first_row, String::new(), max_dist, &mut results);
    }
    results.sort_by(|a, b| a.1.cmp(&b.1).then_with(|| a.0.cmp(&b.0)));
    results
}

fn search_recursive(
    node: &TrieNode,
    ch: char,
    word: &[char],
    prev_row: &[usize],
    prefix: String,
    max_dist: usize,
    results: &mut Vec<(String, usize)>,
) {
    let columns = word.len() + 1;
    let mut curr_row = vec![0usize; columns];
    curr_row[0] = prev_row[0] + 1;

    for i in 1..columns {
        let cost = if word[i - 1] == ch { 0 } else { 1 };
        curr_row[i] = (curr_row[i - 1] + 1)
            .min(prev_row[i] + 1)
            .min(prev_row[i - 1] + cost);
    }

    let mut next_prefix = prefix;
    next_prefix.push(ch);

    if node.is_end && curr_row[columns - 1] <= max_dist {
        results.push((next_prefix.clone(), curr_row[columns - 1]));
    }

    if curr_row.iter().min().copied().unwrap_or(usize::MAX) <= max_dist {
        for (next_ch, child) in node.children.iter() {
            search_recursive(child, *next_ch, word, &curr_row, next_prefix.clone(), max_dist, results);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn distance_identical() {
        assert_eq!(levenshtein("kitten", "kitten"), 0);
    }

    #[test]
    fn distance_classic_example() {
        assert_eq!(levenshtein("kitten", "sitting"), 3);
    }

    #[test]
    fn distance_empty_strings() {
        assert_eq!(levenshtein("", "abc"), 3);
        assert_eq!(levenshtein("abc", ""), 3);
    }

    #[test]
    fn fuzzy_search_finds_near_matches() {
        let mut trie = Trie::new();
        for w in ["cat", "cot", "cart", "dog", "dot"] {
            trie.insert(w, 1);
        }
        let matches = fuzzy_search(&trie, "cat", 1);
        let words: Vec<&str> = matches.iter().map(|(w, _)| w.as_str()).collect();
        assert!(words.contains(&"cat"));
        assert!(words.contains(&"cot"));
        assert!(!words.contains(&"dog"));
    }

    #[test]
    fn fuzzy_search_matches_brute_force() {
        let mut trie = Trie::new();
        let vocab = ["cat", "cot", "cart", "dog", "dot", "dote", "coat", "carts"];
        for w in vocab {
            trie.insert(w, 1);
        }
        for max_dist in 0..=2 {
            let mut expected: Vec<String> = vocab
                .iter()
                .filter(|w| levenshtein(w, "cat") <= max_dist)
                .map(|w| w.to_string())
                .collect();
            expected.sort();
            let mut got: Vec<String> = fuzzy_search(&trie, "cat", max_dist)
                .into_iter()
                .map(|(w, _)| w)
                .collect();
            got.sort();
            assert_eq!(expected, got);
        }
    }
}

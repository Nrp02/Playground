use std::collections::HashMap;

#[derive(Default)]
pub struct TrieNode {
    pub children: HashMap<char, TrieNode>,
    pub is_end: bool,
    pub weight: u32,
}

impl TrieNode {
    fn new() -> Self {
        TrieNode::default()
    }
}

#[derive(Default)]
pub struct Trie {
    pub root: TrieNode,
}

impl Trie {
    pub fn new() -> Self {
        Trie { root: TrieNode::new() }
    }

    pub fn insert(&mut self, word: &str, weight: u32) {
        let mut node = &mut self.root;
        for ch in word.chars() {
            node = node.children.entry(ch).or_insert_with(TrieNode::new);
        }
        node.is_end = true;
        if weight > node.weight {
            node.weight = weight;
        }
    }

    #[allow(dead_code)]
    pub fn contains(&self, word: &str) -> bool {
        match self.node_for_prefix(word) {
            Some(node) => node.is_end,
            None => false,
        }
    }

    pub fn node_for_prefix(&self, prefix: &str) -> Option<&TrieNode> {
        let mut node = &self.root;
        for ch in prefix.chars() {
            node = node.children.get(&ch)?;
        }
        Some(node)
    }

    pub fn words_with_prefix(&self, prefix: &str) -> Vec<(String, u32)> {
        let mut results = Vec::new();
        if let Some(node) = self.node_for_prefix(prefix) {
            collect(node, prefix.to_string(), &mut results);
        }
        results
    }

    pub fn autocomplete(&self, prefix: &str, n: usize) -> Vec<(String, u32)> {
        let mut matches = self.words_with_prefix(prefix);
        matches.sort_by(|a, b| b.1.cmp(&a.1).then_with(|| a.0.cmp(&b.0)));
        matches.truncate(n);
        matches
    }
}

fn collect(node: &TrieNode, prefix: String, out: &mut Vec<(String, u32)>) {
    if node.is_end {
        out.push((prefix.clone(), node.weight));
    }
    for (ch, child) in node.children.iter() {
        let mut next = prefix.clone();
        next.push(*ch);
        collect(child, next, out);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn insert_and_contains() {
        let mut trie = Trie::new();
        trie.insert("cat", 1);
        trie.insert("car", 2);
        trie.insert("cart", 3);
        assert!(trie.contains("cat"));
        assert!(trie.contains("car"));
        assert!(trie.contains("cart"));
        assert!(!trie.contains("ca"));
        assert!(!trie.contains("dog"));
    }

    #[test]
    fn prefix_lookup() {
        let mut trie = Trie::new();
        trie.insert("cat", 1);
        trie.insert("car", 2);
        trie.insert("cart", 3);
        trie.insert("dog", 4);
        let mut words: Vec<String> = trie
            .words_with_prefix("ca")
            .into_iter()
            .map(|(w, _)| w)
            .collect();
        words.sort();
        assert_eq!(words, vec!["car", "cart", "cat"]);
    }

    #[test]
    fn autocomplete_ranks_by_weight() {
        let mut trie = Trie::new();
        trie.insert("cat", 5);
        trie.insert("car", 50);
        trie.insert("cart", 20);
        let top = trie.autocomplete("ca", 2);
        assert_eq!(top[0].0, "car");
        assert_eq!(top[1].0, "cart");
        assert_eq!(top.len(), 2);
    }

    #[test]
    fn empty_prefix_returns_nothing_on_empty_trie() {
        let trie = Trie::new();
        assert!(trie.words_with_prefix("").is_empty());
    }
}

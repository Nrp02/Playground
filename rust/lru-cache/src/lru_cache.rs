use std::collections::HashMap;
use std::hash::Hash;

struct Node<K, V> {
    key: K,
    value: V,
    prev: Option<usize>,
    next: Option<usize>,
}

pub struct LruCache<K, V> {
    capacity: usize,
    map: HashMap<K, usize>,
    nodes: Vec<Option<Node<K, V>>>,
    free: Vec<usize>,
    head: Option<usize>,
    tail: Option<usize>,
}

impl<K: Eq + Hash + Clone, V> LruCache<K, V> {
    pub fn new(capacity: usize) -> Self {
        assert!(capacity > 0);
        LruCache {
            capacity,
            map: HashMap::new(),
            nodes: Vec::new(),
            free: Vec::new(),
            head: None,
            tail: None,
        }
    }

    pub fn len(&self) -> usize {
        self.map.len()
    }

    pub fn is_empty(&self) -> bool {
        self.map.is_empty()
    }

    pub fn capacity(&self) -> usize {
        self.capacity
    }

    pub fn contains_key(&self, key: &K) -> bool {
        self.map.contains_key(key)
    }

    fn unlink(&mut self, idx: usize) {
        let (prev, next) = {
            let node = self.nodes[idx].as_ref().unwrap();
            (node.prev, node.next)
        };
        match prev {
            Some(p) => self.nodes[p].as_mut().unwrap().next = next,
            None => self.head = next,
        }
        match next {
            Some(n) => self.nodes[n].as_mut().unwrap().prev = prev,
            None => self.tail = prev,
        }
        let node = self.nodes[idx].as_mut().unwrap();
        node.prev = None;
        node.next = None;
    }

    fn push_front(&mut self, idx: usize) {
        let old_head = self.head;
        {
            let node = self.nodes[idx].as_mut().unwrap();
            node.prev = None;
            node.next = old_head;
        }
        if let Some(h) = old_head {
            self.nodes[h].as_mut().unwrap().prev = Some(idx);
        }
        self.head = Some(idx);
        if self.tail.is_none() {
            self.tail = Some(idx);
        }
    }

    fn touch(&mut self, idx: usize) {
        if self.head == Some(idx) {
            return;
        }
        self.unlink(idx);
        self.push_front(idx);
    }

    pub fn get(&mut self, key: &K) -> Option<&V> {
        let idx = *self.map.get(key)?;
        self.touch(idx);
        self.nodes[idx].as_ref().map(|n| &n.value)
    }

    pub fn peek(&self, key: &K) -> Option<&V> {
        let idx = *self.map.get(key)?;
        self.nodes[idx].as_ref().map(|n| &n.value)
    }

    fn evict_tail(&mut self) {
        let idx = self.tail.unwrap();
        self.unlink(idx);
        let evicted = self.nodes[idx].take().unwrap();
        self.map.remove(&evicted.key);
        self.free.push(idx);
    }

    pub fn put(&mut self, key: K, value: V) {
        if let Some(&idx) = self.map.get(&key) {
            self.nodes[idx].as_mut().unwrap().value = value;
            self.touch(idx);
            return;
        }
        if self.map.len() >= self.capacity {
            self.evict_tail();
        }
        let idx = match self.free.pop() {
            Some(i) => i,
            None => {
                self.nodes.push(None);
                self.nodes.len() - 1
            }
        };
        self.nodes[idx] = Some(Node {
            key: key.clone(),
            value,
            prev: None,
            next: None,
        });
        self.map.insert(key, idx);
        self.push_front(idx);
    }

    pub fn remove(&mut self, key: &K) -> Option<V> {
        let idx = *self.map.get(key)?;
        self.unlink(idx);
        let node = self.nodes[idx].take().unwrap();
        self.map.remove(key);
        self.free.push(idx);
        Some(node.value)
    }

    pub fn keys_most_to_least_recent(&self) -> Vec<K> {
        let mut result = Vec::with_capacity(self.map.len());
        let mut cur = self.head;
        while let Some(idx) = cur {
            let node = self.nodes[idx].as_ref().unwrap();
            result.push(node.key.clone());
            cur = node.next;
        }
        result
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn basic_insert_and_get() {
        let mut cache: LruCache<i32, &str> = LruCache::new(2);
        cache.put(1, "a");
        cache.put(2, "b");
        assert_eq!(cache.get(&1), Some(&"a"));
        assert_eq!(cache.get(&2), Some(&"b"));
        assert_eq!(cache.len(), 2);
    }

    #[test]
    fn eviction_order_hand_traced() {
        let mut cache: LruCache<i32, i32> = LruCache::new(3);
        cache.put(1, 10);
        cache.put(2, 20);
        cache.put(3, 30);
        assert_eq!(cache.keys_most_to_least_recent(), vec![3, 2, 1]);

        cache.put(4, 40);
        assert_eq!(cache.keys_most_to_least_recent(), vec![4, 3, 2]);
        assert!(!cache.contains_key(&1));

        cache.put(5, 50);
        assert_eq!(cache.keys_most_to_least_recent(), vec![5, 4, 3]);
        assert!(!cache.contains_key(&2));
    }

    #[test]
    fn get_refreshes_recency() {
        let mut cache: LruCache<i32, i32> = LruCache::new(3);
        cache.put(1, 10);
        cache.put(2, 20);
        cache.put(3, 30);
        assert_eq!(cache.keys_most_to_least_recent(), vec![3, 2, 1]);

        assert_eq!(cache.get(&1), Some(&10));
        assert_eq!(cache.keys_most_to_least_recent(), vec![1, 3, 2]);

        cache.put(4, 40);
        assert!(cache.contains_key(&1));
        assert!(!cache.contains_key(&2));
        assert_eq!(cache.keys_most_to_least_recent(), vec![4, 1, 3]);
    }

    #[test]
    fn capacity_one_edge_case() {
        let mut cache: LruCache<i32, i32> = LruCache::new(1);
        cache.put(1, 10);
        assert_eq!(cache.get(&1), Some(&10));
        cache.put(2, 20);
        assert!(!cache.contains_key(&1));
        assert_eq!(cache.get(&2), Some(&20));
        assert_eq!(cache.len(), 1);
    }

    #[test]
    fn overwrite_existing_key_updates_value_and_recency_without_growing_size() {
        let mut cache: LruCache<i32, i32> = LruCache::new(3);
        cache.put(1, 10);
        cache.put(2, 20);
        cache.put(3, 30);
        assert_eq!(cache.len(), 3);

        cache.put(1, 999);
        assert_eq!(cache.len(), 3);
        assert_eq!(cache.get(&1), Some(&999));
        assert_eq!(cache.keys_most_to_least_recent(), vec![1, 3, 2]);
    }

    #[test]
    fn remove_deletes_entry_and_frees_slot() {
        let mut cache: LruCache<i32, i32> = LruCache::new(2);
        cache.put(1, 10);
        cache.put(2, 20);
        assert_eq!(cache.remove(&1), Some(10));
        assert!(!cache.contains_key(&1));
        assert_eq!(cache.len(), 1);

        cache.put(3, 30);
        cache.put(4, 40);
        assert_eq!(cache.len(), 2);
        assert!(!cache.contains_key(&2) || !cache.contains_key(&3));
    }

    #[test]
    fn reused_freed_slots_do_not_corrupt_links() {
        let mut cache: LruCache<i32, i32> = LruCache::new(2);
        cache.put(1, 10);
        cache.put(2, 20);
        cache.put(3, 30);
        cache.put(4, 40);
        cache.put(5, 50);
        assert_eq!(cache.keys_most_to_least_recent(), vec![5, 4]);
        assert_eq!(cache.len(), 2);
    }
}

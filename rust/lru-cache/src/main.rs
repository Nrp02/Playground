mod lru_cache;

use lru_cache::LruCache;

fn main() {
    let mut cache: LruCache<String, i32> = LruCache::new(3);

    cache.put("a".to_string(), 1);
    cache.put("b".to_string(), 2);
    cache.put("c".to_string(), 3);
    println!(
        "after inserting a,b,c (cap 3): most-to-least recent = {:?}",
        cache.keys_most_to_least_recent()
    );

    cache.put("d".to_string(), 4);
    println!(
        "after inserting d (over capacity): most-to-least recent = {:?}",
        cache.keys_most_to_least_recent()
    );
    println!("contains a? {}", cache.contains_key(&"a".to_string()));

    let mut cache2: LruCache<String, i32> = LruCache::new(3);
    cache2.put("x".to_string(), 10);
    cache2.put("y".to_string(), 20);
    cache2.put("z".to_string(), 30);
    println!(
        "\nafter inserting x,y,z (cap 3): most-to-least recent = {:?}",
        cache2.keys_most_to_least_recent()
    );

    let refreshed = cache2.get(&"x".to_string());
    println!("get(x) = {refreshed:?}, refreshes recency");
    println!(
        "most-to-least recent after get(x): {:?}",
        cache2.keys_most_to_least_recent()
    );

    cache2.put("w".to_string(), 40);
    println!(
        "after inserting w (over capacity): most-to-least recent = {:?}",
        cache2.keys_most_to_least_recent()
    );
    println!(
        "x survived the eviction (recency refresh worked): {}",
        cache2.contains_key(&"x".to_string())
    );
    println!(
        "y was evicted instead (was least recently used): {}",
        !cache2.contains_key(&"y".to_string())
    );

    println!(
        "\ncache2 len/capacity/is_empty: {} / {} / {}",
        cache2.len(),
        cache2.capacity(),
        cache2.is_empty()
    );
    println!("peek(x) without changing recency: {:?}", cache2.peek(&"x".to_string()));
    println!("remove(x): {:?}", cache2.remove(&"x".to_string()));
    println!(
        "most-to-least recent after remove(x): {:?}",
        cache2.keys_most_to_least_recent()
    );
}
